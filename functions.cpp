#pragma once

void fusion::debit_user_redemptions_if_necessary(const name& user, const asset& swax_balance){
  //need to know which epochs to check
  state s = states.get();
  config c = configs.get();

  requests_tbl requests_t = requests_tbl(get_self(), user.value);

  eosio::asset total_amount_awaiting_redemption = eosio::asset( 0, WAX_SYMBOL );
  uint64_t first_epoch_to_check = s.last_epoch_start_time - c.seconds_between_epochs;

  std::vector<uint64_t> epochs_to_check = { //reversed since we want to debit the farthest epochs if needed
    first_epoch_to_check + ( c.seconds_between_epochs * 2 ),
    first_epoch_to_check + c.seconds_between_epochs,
    first_epoch_to_check
  };    

  for(uint64_t ep : epochs_to_check){
    auto epoch_itr = epochs_t.find(ep);

    if(epoch_itr != epochs_t.end()){

      auto req_itr = requests_t.find(ep); 

      if(req_itr != requests_t.end()){
        //there is a pending request

        //add the requested amount to total_amount_awaiting_redemption
        total_amount_awaiting_redemption.amount = safeAddInt64( total_amount_awaiting_redemption.amount, req_itr->wax_amount_requested.amount );

      }
    } 
  }  

  if( total_amount_awaiting_redemption.amount > swax_balance.amount ){
    //the user is overdrawn, need to loop through the epochs again and debit the necessary amount
    int64_t amount_overdrawn_i64 = safeSubInt64( total_amount_awaiting_redemption.amount, swax_balance.amount );

    for(uint64_t ep : epochs_to_check){
      auto epoch_itr = epochs_t.find(ep);

      if(epoch_itr != epochs_t.end()){

        auto req_itr = requests_t.find(ep); 

        if(req_itr != requests_t.end()){
          //there is a pending request

          //if the amount requested is > amount_overdrawn_i64, only subtract the difference, and break
          if( req_itr->wax_amount_requested.amount > amount_overdrawn_i64 ){
            requests_t.modify(req_itr, same_payer, [&](auto &_r){
              _r.wax_amount_requested.amount = safeSubInt64( _r.wax_amount_requested.amount, amount_overdrawn_i64 );
            });

            epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
              _e.wax_to_refund.amount = safeSubInt64( _e.wax_to_refund.amount, amount_overdrawn_i64 );
            });

            break;
          }

          else if( req_itr->wax_amount_requested.amount == amount_overdrawn_i64 ){
            epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
              _e.wax_to_refund.amount = safeSubInt64( _e.wax_to_refund.amount, amount_overdrawn_i64 );
            });

            req_itr = requests_t.erase( req_itr );

            break;
          }

          else{ //amount requested is < the overdrawn amount
            amount_overdrawn_i64 = safeSubInt64( amount_overdrawn_i64, req_itr->wax_amount_requested.amount );

            epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
              _e.wax_to_refund.amount = safeSubInt64( _e.wax_to_refund.amount, req_itr->wax_amount_requested.amount );
            });       

            req_itr = requests_t.erase( req_itr );     
          }

        }
      } 
    }  

  }

  return;
}

std::string fusion::cpu_stake_memo(const eosio::name& cpu_receiver, const uint64_t& epoch_timestamp){
  return ("|stake_cpu|" + cpu_receiver.to_string() + "|" + std::to_string(epoch_timestamp) + "|").c_str();
}

std::vector<std::string> fusion::get_words(std::string memo){
  std::string delim = "|";
  std::vector<std::string> words{};
  size_t pos = 0;
  while ((pos = memo.find(delim)) != std::string::npos) {
    words.push_back(memo.substr(0, pos));
    memo.erase(0, pos + delim.length());
  }
  return words;
}

/**
* is_an_admin
* this contract is multisig and all of the most important features will require msig
* there will be some features that do not pose any major risk to users/funds
* in which case, just having a few "admin" accounts who can call certain actions will be sufficient
* rather than needing to organize an msig with multiple parties over a minor adjustment
* e.g. - adjusting the cost for renting CPU or adjusting the fallback_cpu_receiver etc
*/

bool fusion::is_an_admin(const eosio::name& user){
  config c = configs.get();

  if( std::find(c.admin_wallets.begin(), c.admin_wallets.end(), user) != c.admin_wallets.end() ){
    return true;
  }

  return false;
}

bool fusion::is_cpu_contract(const eosio::name& contract){
  config c = configs.get();

  if( std::find( c.cpu_contracts.begin(), c.cpu_contracts.end(), contract) != c.cpu_contracts.end() ){
    return true;
  }

  return false;
}

void fusion::issue_lswax(const int64_t& amount, const eosio::name& receiver){
  action(permission_level{get_self(), "active"_n}, TOKEN_CONTRACT,"issue"_n,std::tuple{ get_self(), receiver, eosio::asset(amount, LSWAX_SYMBOL), std::string("issuing lsWAX to liquify")}).send();
  return;
}

void fusion::issue_swax(const int64_t& amount){
  action(permission_level{get_self(), "active"_n}, TOKEN_CONTRACT,"issue"_n,std::tuple{ get_self(), get_self(), eosio::asset(amount, SWAX_SYMBOL), std::string("issuing sWAX for staking")}).send();
  return;
}

bool fusion::memo_is_expected(const std::string& memo){
  if( memo == "wax_lswax_liquidity" || memo == "stake" || memo == "unliquify" || memo == "waxfusion_revenue" || memo == "cpu rental return" ){
    return true;
  }

  std::string memo_copy = memo;
  std::vector<std::string> words = get_words(memo_copy);

  if( words[1] == "rent_cpu" || words[1] == "unliquify_exact" ){
      return true;
  }  

  return false;
}

uint64_t fusion::now(){
  return current_time_point().sec_since_epoch();
}

void fusion::retire_lswax(const int64_t& amount){
  action(permission_level{get_self(), "active"_n}, TOKEN_CONTRACT,"retire"_n,std::tuple{ get_self(), eosio::asset(amount, LSWAX_SYMBOL), std::string("retiring lsWAX to unliquify")}).send();
  return;
}

void fusion::retire_swax(const int64_t& amount){
  action(permission_level{get_self(), "active"_n}, TOKEN_CONTRACT,"retire"_n,std::tuple{ get_self(), eosio::asset(amount, SWAX_SYMBOL), std::string("retiring sWAX for redemption")}).send();
  return;
}

void fusion::sync_epoch(){
  //find out when the last epoch started
  state s = states.get();
  config c = configs.get();

  int next_cpu_index = 1;
  bool contract_was_found = false;

  for(eosio::name cpu : c.cpu_contracts){

    if( cpu == s.current_cpu_contract ){
      contract_was_found = true;

      if(next_cpu_index == c.cpu_contracts.size()){
        next_cpu_index = 0;
      }
    }

    if(contract_was_found) break;
    next_cpu_index ++;
  }

  check( contract_was_found, "error locating cpu contract" );
  eosio::name next_cpu_contract = c.cpu_contracts[next_cpu_index];
  check( next_cpu_contract != s.current_cpu_contract, "next cpu contract cant be the same as the current contract" );

  //calculate when the next is supposed to start
  uint64_t next_epoch_start_time = s.last_epoch_start_time + c.seconds_between_epochs;

  if( now() >= next_epoch_start_time ){

    s.last_epoch_start_time = next_epoch_start_time;
    s.current_cpu_contract = next_cpu_contract;
    states.set(s, _self);

    auto epoch_itr = epochs_t.find(next_epoch_start_time);

    //this epoch should already exist due to CPU staking, but there's a possibility no CPU has been staked to it yet
    if(epoch_itr == epochs_t.end()){

      epochs_t.emplace(get_self(), [&](auto &_e){
        _e.start_time = next_epoch_start_time;
        /* unstake 3 days before epoch ends */
        _e.time_to_unstake = next_epoch_start_time + c.cpu_rental_epoch_length_seconds - (60 * 60 * 24 * 3);
        _e.cpu_wallet = next_cpu_contract;
        _e.wax_bucket = ZERO_WAX;
        _e.wax_to_refund = ZERO_WAX;
        /* redemption starts at the end of the epoch, ends 48h later */
        _e.redemption_period_start_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds;
        _e.redemption_period_end_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds + c.redemption_period_length_seconds;
        _e.total_cpu_funds_returned = ZERO_WAX;
        _e.total_added_to_redemption_bucket = ZERO_WAX;
      });

    }
  }

  return;
}

void fusion::sync_user(const eosio::name& user){
  auto staker = staker_t.require_find(user.value, "you need to use the stake action first");

  const uint64_t lower_bound_timestamp = staker->last_update + 1;

  //is their last_update < last_payout ?
  auto low_itr = snaps_t.lower_bound( lower_bound_timestamp );

  if(low_itr == snaps_t.end()) return;

  config c = configs.get();
  state s = states.get();

  int count = 0;
  int64_t wax_owed_to_user = 0; 

  /* if they have no staked sWAX, they get 0 */
  if(staker->swax_balance.amount > 0){

    for(auto it = low_itr; it != snaps_t.end(); it++){
      //only calculate if there was sWAX earning
      //redundant safety check to make sure the snapshot timestamp is eligible
      if(it->swax_earning_bucket.amount > 0 && it->timestamp >= lower_bound_timestamp){
        //calculate the % of snapshot owned by this user
        double percentage_allocation = safeDivDouble( (double) staker->swax_balance.amount, (double) it->total_swax_earning.amount );

        //use that to get a wax quantity (needs to be positive)
        double wax_allocation = safeMulDouble( (double) it->swax_earning_bucket.amount, percentage_allocation );
       
        //add that quantity to wax_owed_to_user
        wax_owed_to_user = safeAddInt64(wax_owed_to_user, (int64_t) wax_allocation);
      }

      count ++;
      if(count >= c.max_snapshots_to_process) break;
    }
  }

  asset claimable_wax = staker->claimable_wax;
  claimable_wax.amount = safeAddInt64(claimable_wax.amount, wax_owed_to_user);

  //credit their balance, update last_update to now()
  staker_t.modify(staker, same_payer, [&](auto &_s){
    _s.claimable_wax = claimable_wax;
    _s.last_update = now();
  });

  //debit the user bucket in state
  s.user_funds_bucket.amount = safeSubInt64(s.user_funds_bucket.amount, wax_owed_to_user);
  states.set(s, _self);

  return;
}

void fusion::transfer_tokens(const name& user, const asset& amount_to_send, const name& contract, const std::string& memo){
  action(permission_level{get_self(), "active"_n}, contract,"transfer"_n,std::tuple{ get_self(), user, amount_to_send, memo}).send();
  return;
}

void fusion::validate_token(const eosio::symbol& symbol, const eosio::name& contract)
{
  if(symbol == WAX_SYMBOL){
    check(contract == WAX_CONTRACT, "expected eosio.token contract");
    return;
  }

  if(symbol == LSWAX_SYMBOL){
    check(contract == TOKEN_CONTRACT, "expected token.fusion contract");
    return;
  }  

  check(false, "invalid token received");
}

void fusion::zero_distribution(){
  config c = configs.get();
  state s = states.get();

  snaps_t.emplace(get_self(), [&](auto &_snap){
    _snap.timestamp = s.next_distribution;
    _snap.swax_earning_bucket = ZERO_WAX;
    _snap.lswax_autocompounding_bucket = ZERO_WAX;
    _snap.pol_bucket = ZERO_WAX;
    _snap.ecosystem_bucket = ZERO_WAX;
    _snap.total_distributed = ZERO_WAX; 
    _snap.total_swax_earning = ZERO_SWAX; 
  });  

  s.next_distribution += c.seconds_between_distributions;
  states.set(s, _self);    
}