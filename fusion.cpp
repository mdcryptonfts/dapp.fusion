#include "fusion.hpp"
#include "functions.cpp"
#include "safe.cpp"
#include "on_notify.cpp"


ACTION fusion::addadmin(const eosio::name& admin_to_add){
	require_auth(_self);
	check( is_account(admin_to_add), "admin_to_add is not a wax account" );

	config c = configs.get();

	if( std::find( c.admin_wallets.begin(), c.admin_wallets.end(), admin_to_add ) == c.admin_wallets.end() ){
		c.admin_wallets.push_back( admin_to_add );
		configs.set(c, _self);
	} else {
		check( false, ( admin_to_add.to_string() + " is already an admin" ).c_str() );
	}
}

ACTION fusion::addcpucntrct(const eosio::name& contract_to_add){
	require_auth(_self);
	check( is_account(contract_to_add), "contract_to_add is not a wax account" );

	config c = configs.get();

	if( std::find( c.cpu_contracts.begin(), c.cpu_contracts.end(), contract_to_add ) == c.cpu_contracts.end() ){
		c.cpu_contracts.push_back( contract_to_add );
		configs.set(c, _self);
	} else {
		check( false, ( contract_to_add.to_string() + " is already a cpu contract" ).c_str() );
	}
}

ACTION fusion::claimrewards(const eosio::name& user){
	/* converting a % to powerup should be bundled in on the front end (i.e. ignored here) */

	require_auth(user);
	sync_user(user);

	auto staker = staker_t.require_find(user.value, "you don't have anything staked here");
	if(staker->claimable_wax.amount > 0){
		transfer_tokens( user, staker->claimable_wax, WAX_CONTRACT, std::string("your sWAX reward claim from waxfusion.io - liquid staking protocol") );

		staker_t.modify(staker, same_payer, [&](auto &_s){
			_s.claimable_wax.amount = 0;
		});

		return;
	}

	check(false, "you have nothing to claim");
}

ACTION fusion::distribute(){
	//should anyone be able to call this?

	sync_epoch();

	//get the config
	config c = configs.get();

	//get the state
	state s = states.get();

	//make sure its been long enough since the last distribution
	if( s.next_distribution > now() ){
		check( false, ("next distribution is not until " + std::to_string(s.next_distribution) ).c_str() );
	}

	if( s.revenue_awaiting_distribution.amount == 0 ){
		//do a zero distribution (avoid conditional safemath below)
		zero_distribution();
		return;
	}

	//amount to distribute = revenue_awaiting_distribution
	double amount_to_distribute = (double) s.revenue_awaiting_distribution.amount;
	double user_allocation = amount_to_distribute * c.user_share;
	double pol_allocation = amount_to_distribute * c.pol_share;
	double ecosystem_share = amount_to_distribute - user_allocation - pol_allocation;

	double sum_of_sWAX_and_lsWAX = (double) s.swax_currently_earning.amount + (double) s.swax_currently_backing_lswax.amount;

	double swax_currently_earning_allocation = 
		user_allocation 
		* 
		safeDivDouble( (double) s.swax_currently_earning.amount, sum_of_sWAX_and_lsWAX );

	double autocompounding_allocation = 
		user_allocation 
		* 
		safeDivDouble( (double) s.swax_currently_backing_lswax.amount, sum_of_sWAX_and_lsWAX );		

	//issue sWAX
	issue_swax( (int64_t) autocompounding_allocation );

	//increase the backing of lsWAX with the newly issued sWAX
	s.swax_currently_backing_lswax.amount = safeAddInt64( s.swax_currently_backing_lswax.amount, (int64_t) autocompounding_allocation );


	/** 
	* TODO: add more safety checks now that the addition of allocating user funds between s/lsWAX has taken place
	* also need more safemath functions, specifically for adding doubles and multiplying int64_t / double
	*/



	//i64 allocations
	int64_t amount_to_distribute_i64 = (int64_t) amount_to_distribute;
	int64_t user_alloc_i64 = (int64_t) user_allocation;
	int64_t swax_earning_alloc_i64 = (int64_t) swax_currently_earning_allocation;
	int64_t swax_autocompounding_alloc_i64 = (int64_t) autocompounding_allocation;
	int64_t pol_alloc_i64 = (int64_t) pol_allocation;
	int64_t eco_alloc_i64 = (int64_t) ecosystem_share;

	//check the sum is in range
	int64_t alloc_check_1 = safeAddInt64(user_alloc_i64, pol_alloc_i64);
	int64_t alloc_check_2 = safeAddInt64(alloc_check_1, eco_alloc_i64);
	check( alloc_check_2 <= amount_to_distribute_i64, "allocation check 2 failed" );

	//set revenue_awaiting_distribution to 0
	s.revenue_awaiting_distribution.amount = 0;

	//user share goes to s.user_funds_bucket
	s.user_funds_bucket.amount = safeAddInt64(s.user_funds_bucket.amount, swax_earning_alloc_i64);

	//pol share goes to POL_CONTRACT
	transfer_tokens( POL_CONTRACT, asset(pol_alloc_i64, WAX_SYMBOL), WAX_CONTRACT, std::string("pol allocation from waxfusion distribution") );

	//loop through ecosystem_fund and store each of their share in ecosystem table 

	double total_paid_to_eco = 0;
	for(auto e : c.ecosystem_fund){
		double allocation = e.amount * ecosystem_share;
		int64_t allocation_i64 = (int64_t) allocation;
		total_paid_to_eco += allocation;

		auto eco_it = eco_t.find(e.beneficiary.value);
		if( eco_it == eco_t.end() ){
			eco_t.emplace(get_self(), [&](auto &_eco){
				_eco.beneficiary = e.beneficiary;
				_eco.wax_balance = asset(allocation_i64, WAX_SYMBOL);
				_eco.total_wax_received = asset(allocation_i64, WAX_SYMBOL);		
			});
		} else {
			int64_t new_balance = safeAddInt64(eco_it->wax_balance.amount, allocation_i64);
			int64_t updated_total = safeAddInt64(eco_it->total_wax_received.amount, allocation_i64);

			eco_t.modify(eco_it, same_payer, [&](auto &_eco){
				_eco.wax_balance = asset(new_balance, WAX_SYMBOL);
				_eco.total_wax_received = asset(updated_total, WAX_SYMBOL);
			});
		}
	}

	check( total_paid_to_eco <= ecosystem_share, "overdrawn ecosystem allocation" );

	//create a snapshot
	snaps_t.emplace(get_self(), [&](auto &_snap){
		_snap.timestamp = s.next_distribution;
		_snap.swax_earning_bucket = asset(swax_earning_alloc_i64, WAX_SYMBOL);
		_snap.lswax_autocompounding_bucket = asset(swax_autocompounding_alloc_i64, WAX_SYMBOL);
		_snap.pol_bucket = asset(pol_alloc_i64, WAX_SYMBOL);
		_snap.ecosystem_bucket = asset(eco_alloc_i64, WAX_SYMBOL);
		_snap.total_distributed = asset(amount_to_distribute_i64, WAX_SYMBOL);	
	});

	//update total_revenue_distributed in state
	s.total_revenue_distributed.amount = safeAddInt64(s.total_revenue_distributed.amount, amount_to_distribute_i64);

	//update next_dist in the state table
	s.next_distribution += c.seconds_between_distributions;

    s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, swax_autocompounding_alloc_i64);

    //check when the last epoch started, calculate next epoch start
    //upsert epoch and add this to the bucket
    uint64_t next_epoch_start_time = s.last_epoch_start_time += c.seconds_between_epochs;
    auto epoch_itr = epochs_t.find(next_epoch_start_time);

    if(epoch_itr == epochs_t.end()){
    	epochs_t.emplace(get_self(), [&](auto &_e){
    		_e.start_time = next_epoch_start_time;
    		/* unstake 3 days before epoch ends */
    		_e.time_to_unstake = next_epoch_start_time + c.cpu_rental_epoch_length_seconds - (60 * 60 * 24 * 3);
    		_e.cpu_wallet = "idk"_n; /* how do we figure this out, and do we transfer there now? */
    		_e.wax_bucket = asset(swax_autocompounding_alloc_i64, WAX_SYMBOL);
    		_e.wax_to_refund = ZERO_WAX;
    	});
    } else {
    	/* TODO: safemath for the addition to wax_bucket */
    	
    	epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
    		_e.wax_bucket += asset(swax_autocompounding_alloc_i64, WAX_SYMBOL);
    	});
    }

    states.set(s, _self);

	return;	

}

ACTION fusion::initconfig(){
	require_auth(get_self());

	//eosio::check(!configs.exists(), "Config already exists");
	//eosio::check(!states.exists(), "State already exists");

	double eco_split = (double) 1 / (double) 6;

	config c{};
	c.minimum_stake_amount = eosio::asset(100000000, WAX_SYMBOL);
	c.minimum_unliquify_amount = eosio::asset(100000000, LSWAX_SYMBOL);
	c.seconds_between_distributions = 86400;
	c.max_snapshots_to_process = 180;
	c.initial_epoch_start_time = INITIAL_EPOCH_START_TIMESTAMP;
	c.cpu_rental_epoch_length_seconds = 60 * 60 * 24 * 14; /* 14 days */
	c.seconds_between_epochs = 60 * 60 * 24 * 7; /* 7 days */
	c.user_share = 0.85;
	c.pol_share = 0.1;
	c.ecosystem_fund = {
		{"nefty"_n, eco_split },
		{"hive"_n, eco_split },
		{"waxdao"_n, eco_split },
		{"wombat"_n, eco_split },
		{"taco"_n, eco_split },
		{"alcor"_n, eco_split }
	};
	c.admin_wallets = {
		"guild.waxdao"_n,
		"oig"_n,
		_self
		//"admin.wax"_n
	};
	c.cpu_contracts = {
		"cpu1.fusion"_n,
		"cpu2.fusion"_n,
		"cpu3.fusion"_n
	};
	c.redemption_period_length_seconds = 60 * 60 * 24 * 2; /* 2 days */
	c.seconds_between_stakeall = 60 * 60 * 24; /* once per day */
	c.fallback_cpu_receiver = "updatethings"_n;
	configs.set(c, _self);

	state s{};
	s.swax_currently_earning = ZERO_SWAX;
	s.swax_currently_backing_lswax = ZERO_SWAX;
	s.liquified_swax = ZERO_LSWAX;
	s.revenue_awaiting_distribution = ZERO_WAX;
	s.user_funds_bucket = ZERO_WAX;
	s.total_revenue_distributed = ZERO_WAX;
	s.next_distribution = now();
	s.wax_for_redemption = ZERO_WAX;
	s.redemption_period_start = 0;
	s.redemption_period_end = 0;
	s.last_epoch_start_time = INITIAL_EPOCH_START_TIMESTAMP;
	s.wax_available_for_rentals = ZERO_WAX;
	s.cost_to_rent_1_wax = asset(1000000, WAX_SYMBOL); /* 0.01 WAX per day */
	s.current_cpu_contract = "cpu1.fusion"_n;
	s.next_stakeall_time = INITIAL_EPOCH_START_TIMESTAMP + 60 * 60 * 24; /* 1 day */
	states.set(s, _self);

	//create the first epoch

	epochs_t.emplace(get_self(), [&](auto &_e){
		_e.start_time = INITIAL_EPOCH_START_TIMESTAMP;
		/* unstake 3 days before epoch ends */
		_e.time_to_unstake = INITIAL_EPOCH_START_TIMESTAMP + (60 * 60 * 24 * 14) - (60 * 60 * 24 * 3);
		_e.cpu_wallet = "cpu1.fusion"_n;
		_e.wax_bucket = ZERO_WAX;
		_e.wax_to_refund = ZERO_WAX;
		/* redemption starts at the end of the epoch, ends 48h later */
		_e.redemption_period_start_time = INITIAL_EPOCH_START_TIMESTAMP + (60 * 60 * 24 * 14);
		_e.redemption_period_end_time = INITIAL_EPOCH_START_TIMESTAMP + (60 * 60 * 24 * 16);
		_e.total_cpu_funds_returned = ZERO_WAX;
		_e.total_added_to_redemption_bucket = ZERO_WAX;
	});	
}

ACTION fusion::liquify(const eosio::name& user, const eosio::asset& quantity){
	require_auth(user);
    check(quantity.amount > 0, "Invalid quantity.");
    check(quantity.amount < MAX_ASSET_AMOUNT, "quantity too large");
    check(quantity.symbol == SWAX_SYMBOL, "only SWAX can be liquified");	

    //process any payouts for this user since their last interaction
    sync_user(user);

    //make sure they have a row here
	auto staker = staker_t.require_find(user.value, "you have nothing to liquify");

	if(staker->swax_balance < quantity){
		check(false, "you are trying to liquify more than you have");
	}

	//debit requested amount from their staked balance
	staker_t.modify(staker, same_payer, [&](auto &_s){
		_s.swax_balance -= quantity;
		_s.last_update = now();
	});

	//get the current state table
	state s = states.get();

	//calculate equivalent amount of lsWAX (BEFORE adjusting sWAX amounts)
	double lsWAX_per_sWAX;

	//need to account for initial period where the values are still 0
	if( s.liquified_swax.amount == 0 && s.swax_currently_backing_lswax.amount == 0 ){
		lsWAX_per_sWAX = (double) 1;
	} else {
		lsWAX_per_sWAX = safeDivDouble((double) s.liquified_swax.amount, (double) s.swax_currently_backing_lswax.amount);
	}

	double converted_lsWAX_amount = lsWAX_per_sWAX * (double) quantity.amount;
	int64_t converted_lsWAX_i64 = (int64_t) converted_lsWAX_amount;	

	//subtract swax amount from swax_currently_earning
	s.swax_currently_earning.amount = safeSubInt64(s.swax_currently_earning.amount, quantity.amount);

	//add swax amount to swax_currently_backing_swax
	s.swax_currently_backing_lswax.amount = safeAddInt64(s.swax_currently_backing_lswax.amount, quantity.amount);

	//issue the converted lsWAX amount to the user
	issue_lswax(converted_lsWAX_i64, user);

	//add the issued amount to liquified_swax
	s.liquified_swax.amount = safeAddInt64(s.liquified_swax.amount, converted_lsWAX_i64);

	//apply the changes to state table
	states.set(s, _self);

	return;
}

/**
* reallocate
* used for taking any funds that were requested to be redeemed, but werent redeemed in time
*/ 

ACTION fusion::reallocate(){
	//should anyone be able to call this? all it does is move unredeemed to available_for_rental, so probably yes

	sync_epoch();

	//get the last epoch start time
	state s = states.get();
	config c = configs.get();

	//if now > epoch start time + 48h, it means redemption is over
	check( now() > s.last_epoch_start_time + c.redemption_period_length_seconds, "redemption period has not ended yet" );

	//move funds from redemption pool to rental pool
	check( s.wax_for_redemption.amount > 0, "there is no wax to reallocate" );

	s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, s.wax_for_redemption.amount);
	s.wax_for_redemption = ZERO_WAX;

	states.set(s, _self);
}

ACTION fusion::redeem(const eosio::name& user){
	require_auth(user);
	sync_user(user);
	sync_epoch();

	//find out if there is a current redemption period, and when
	state s = states.get();
	config c = configs.get();

	uint64_t redemption_start_time = s.last_epoch_start_time;
	uint64_t redemption_end_time = s.last_epoch_start_time + c.redemption_period_length_seconds;
 
	check( now() < redemption_end_time, 
		( "next redemption does not start until " + std::to_string(s.last_epoch_start_time + c.seconds_between_epochs) ).c_str() 
	);

	//find if the user has a request for this period
	requests_tbl requests_t = requests_tbl(get_self(), redemption_start_time);
	auto req_itr = requests_t.require_find(user.value, "you don't have a redemption request for the current redemption period");

	//if they do, make sure the amount is <= their swax amount
	auto staker = staker_t.require_find(user.value, "you are not staking any sWAX");

	check( req_itr->wax_amount_requested.amount <= staker->swax_balance.amount, "you are trying to redeem more than you have" );

	//make sure s.wax_for_redemption has enough for them (it always should!)
	check( s.wax_for_redemption >= req_itr->wax_amount_requested, "not enough wax in the redemption pool" );

	//subtract the amount from s.wax_for_redemption
	s.wax_for_redemption.amount = safeSubInt64(s.wax_for_redemption.amount, req_itr->wax_amount_requested.amount);

	//subtract the requested amount from their swax balance
	asset updated_swax_balance = staker->swax_balance;
	updated_swax_balance.amount = safeSubInt64(updated_swax_balance.amount, req_itr->wax_amount_requested.amount);

	staker_t.modify(staker, same_payer, [&](auto &_s){
		_s.swax_balance = updated_swax_balance;
	});

	//retire the sWAX
	retire_swax(req_itr->wax_amount_requested.amount);

	//update the swax_currently_earning amount
	s.swax_currently_earning.amount = safeSubInt64(s.swax_currently_earning.amount, req_itr->wax_amount_requested.amount);

	//transfer wax to the user
	transfer_tokens( user, req_itr->wax_amount_requested, WAX_CONTRACT, std::string("your sWAX redemption from waxfusion.io - liquid staking protocol") );

	//erase the request
	req_itr = requests_t.erase(req_itr);
}


ACTION fusion::removeadmin(const eosio::name& admin_to_remove){
	require_auth(_self);

	config c = configs.get();

    auto itr = std::remove(c.admin_wallets.begin(), c.admin_wallets.end(), admin_to_remove);

    if (itr != c.admin_wallets.end()) {
        c.admin_wallets.erase(itr, c.admin_wallets.end());
        configs.set(c, _self);
    } else {
        check(false, (admin_to_remove.to_string() + " is not an admin").c_str());
    }
}

/**
* reqredeem (request redeem)
* initiates a redemption request
* the contract will automatically figure out which epoch(s) have enough wax available
* the user must claim (redeem) their wax within 2 days of it becoming available, or it will be restaked
*/ 

ACTION fusion::reqredeem(const eosio::name& user, const eosio::asset& swax_to_redeem){
	require_auth(user);
	sync_user(user);
	sync_epoch();

	//make sure the amount to redeem is not more than their balance
	auto staker = staker_t.require_find(user.value, "you are not staking any sWAX");
	check(staker->swax_balance >= swax_to_redeem, "you are trying to redeem more than you have");

    check( swax_to_redeem.amount > 0, "Must redeem a positive quantity" );
    check( swax_to_redeem.amount < MAX_ASSET_AMOUNT, "quantity too large" );

	/** 
	* figure out which epoch(s) to take this from, update the epoch(s) to reflect the request
	* first need to find out the epoch linked to the unstake action that is closest to taking place
	* does that have any wax available?
	* if so, put as much of this request into that epoch as possible
	* if there is anything left, then check the next epoch too
	* if that has anything available, repeat
	* can do this one more time if there is a 3rd epoch available
	*/

	state s = states.get();
	config c = configs.get();

	bool request_can_be_filled = false;
	eosio::asset remaining_amount_to_fill = swax_to_redeem;
	uint64_t epoch_to_request_from = s.last_epoch_start_time - c.seconds_between_epochs;

	std::vector<uint64_t> epochs_to_check = {
		epoch_to_request_from,
		epoch_to_request_from + c.seconds_between_epochs,
		epoch_to_request_from + ( c.seconds_between_epochs * 2 )
	};

	/** 
	* loop through the 3 redemption scopes and if the user has any reqs,
	* delete them and sub the amounts from epoch_itr->wax_to_refund
	*/

	for(uint64_t ep : epochs_to_check){
		auto epoch_itr = epochs_t.find(ep);

		if(epoch_itr != epochs_t.end()){
			requests_tbl requests_t = requests_tbl(get_self(), epoch_itr->redemption_period_start_time);
			auto req_itr = requests_t.find(user.value);	

			if(req_itr != requests_t.end()){
				//there is a pending request

				//subtract the pending amount from epoch_itr->wax_to_refund
				int64_t updated_refunding_amount = safeSubInt64(epoch_itr->wax_to_refund.amount, req_itr->wax_amount_requested.amount);

				epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
					_e.wax_to_refund = asset(updated_refunding_amount, WAX_SYMBOL);
				});

				//erase the request
				req_itr = requests_t.erase(req_itr);
			}
		}	
	}


	/**
	* now loop through them again and process them
	* if request becomes filled, break out of the loop
	*/

	for(uint64_t ep : epochs_to_check){
		auto epoch_itr = epochs_t.find(ep);

		if(epoch_itr != epochs_t.end()){

			//see if the deadline for redeeming has passed yet
			if(epoch_itr->time_to_unstake > now()){

				if(epoch_itr->wax_to_refund < epoch_itr->wax_bucket){
					//there are still funds available for redemption

					int64_t amount_available = safeSubInt64(epoch_itr->wax_bucket.amount, epoch_itr->wax_to_refund.amount);

					if(amount_available >= remaining_amount_to_fill.amount){
						//this epoch has enough to cover the whole request
						request_can_be_filled = true;

						int64_t updated_refunding_amount = safeAddInt64(epoch_itr->wax_to_refund.amount, remaining_amount_to_fill.amount);

						//add the amount to the epoch's wax_to_refund
						epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
							_e.wax_to_refund = asset(updated_refunding_amount, WAX_SYMBOL);
						});

						/** 
						* INSERT this request into the request_tbl
						* (any previous records should have been deleted first)
						*/

						requests_tbl requests_t = requests_tbl(get_self(), epoch_itr->redemption_period_start_time);
						auto req_itr = requests_t.find(user.value);

						check( req_itr == requests_t.end(), "user has an existing redemption request in this epoch" );

						requests_t.emplace(user, [&](auto &_r){
							_r.wallet = user;
							_r.wax_amount_requested = asset(remaining_amount_to_fill.amount, WAX_SYMBOL);
						});


					} else {
						//this epoch has some funds, but not enough for the whole request
						int64_t updated_refunding_amount = safeAddInt64(epoch_itr->wax_to_refund.amount, amount_available);

						//debit the amount remaining so we are checking an updated number on the next loop
						remaining_amount_to_fill.amount = safeSubInt64(remaining_amount_to_fill.amount, amount_available);

						epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
							_e.wax_to_refund = asset(updated_refunding_amount, WAX_SYMBOL);
						});

						requests_tbl requests_t = requests_tbl(get_self(), epoch_itr->redemption_period_start_time);
						auto req_itr = requests_t.find(user.value);

						check( req_itr == requests_t.end(), "user has an existing redemption request in this epoch" );
						
						requests_t.emplace(user, [&](auto &_r){
							_r.wallet = user;
							_r.wax_amount_requested = asset(amount_available, WAX_SYMBOL);
						});
					}
				}
			}

		}	

		if( request_can_be_filled ) break;
	}	 

	check( request_can_be_filled, "There is not enough wax available to fill this request yet" );

}

ACTION fusion::rmvcpucntrct(const eosio::name& contract_to_remove){
	require_auth(_self);

	config c = configs.get();

    auto itr = std::remove(c.cpu_contracts.begin(), c.cpu_contracts.end(), contract_to_remove);

    if (itr != c.cpu_contracts.end()) {
        c.cpu_contracts.erase(itr, c.cpu_contracts.end());
        configs.set(c, _self);
    } else {
        check(false, (contract_to_remove.to_string() + " is not a cpu contract").c_str());
    }
}

ACTION fusion::setfallback(const eosio::name& caller, const eosio::name& receiver){
	require_auth(caller);
	check( is_an_admin(caller), "this action requires auth from one of the admin_wallets in the config table" );
	check( is_account(receiver), "cpu receiver is not a wax account" );

	config c = configs.get();
	c.fallback_cpu_receiver = receiver;
	configs.set(c, _self);
}

ACTION fusion::setrentprice(const eosio::name& caller, const eosio::asset& cost_to_rent_1_wax){
	require_auth(caller);
	check( is_an_admin(caller), "this action requires auth from one of the admin_wallets in the config table" );
	check( cost_to_rent_1_wax.amount > 0, "cost must be positive" );
	check( cost_to_rent_1_wax.symbol == WAX_SYMBOL, "symbol and precision must match WAX" );

	state s = states.get();
	s.cost_to_rent_1_wax = cost_to_rent_1_wax;
	states.set(s, _self);
}

ACTION fusion::stake(const eosio::name& user){
	require_auth(user);

	auto staker = staker_t.find(user.value);

	if(staker != staker_t.end()){
		sync_user(user);
		return;
	}

	staker_t.emplace(user, [&](auto &_s){
		_s.wallet = user;
		_s.swax_balance = ZERO_SWAX;
		_s.claimable_wax = ZERO_WAX;
		_s.last_update = now();
	});
}

/**
* stakeallcpu
* once every 24h, this can be called to take any un-rented wax and just stake it so it earns the normal amount
*/ 

ACTION fusion::stakeallcpu(){
	//should anyone be able to call this? probably

	//who should receive the CPU?

	sync_epoch();

	//get the last epoch start time
	state s = states.get();
	config c = configs.get();

	//if now > epoch start time + 48h, it means redemption is over
	check( now() >= s.next_stakeall_time, ( "next stakeall time is not until " + std::to_string(s.next_stakeall_time) ).c_str() );

	if(s.wax_available_for_rentals.amount > 0){
		//stake it

		/** 
		* we can get the cpu contract by taking the last epoch start time (i.e. lets say epoch 3 started)
		* then find out the cpu contract of the next epoch (epoch 4)
		* this will also be the same as going back 2 epochs (epoch 1)
		* the main issue here is we need to make sure this does not cancel any unstakes (the time matches exactly)
		* so, before we send - we need to check and make sure that 2 epochs ago has been fully returned already
		*/

		//first need the timestamp AND cpu contract of the current epoch

		//then we can just get the next contract in line and next epoch in line
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
		check( next_cpu_contract != s.current_cpu_contract, "next cpu contract can not be the same as the current contract" );

		//then we can also see if there is an epoch that exists with the timetamp from 2 epochs ago
		uint64_t next_epoch_start_time = s.last_epoch_start_time + c.seconds_between_epochs;
		uint64_t two_epochs_ago = s.last_epoch_start_time - (c.seconds_between_epochs * 2);

		//if so, check that its been fully refunded
		auto epoch_itr = epochs_t.find(two_epochs_ago);
		if(epoch_itr != epochs_t.end()){
			//an epoch was found - make sure it's been refunded
			check( epoch_itr->total_cpu_funds_returned >= epoch_itr->wax_bucket, (next_cpu_contract.to_string() + " still has funds tied up").c_str() );
		}

		transfer_tokens( s.current_cpu_contract, s.wax_available_for_rentals, WAX_CONTRACT, cpu_stake_memo(c.fallback_cpu_receiver, next_epoch_start_time) );

		//upsert the epoch that it was staked to, so it reflects the added wax
		auto next_epoch_itr = epochs_t.find(next_epoch_start_time);

		if(next_epoch_itr == epochs_t.end()){
			//create new epoch
			epochs_t.emplace(get_self(), [&](auto &_e){
				_e.start_time = next_epoch_start_time;
				/* unstake 3 days before epoch ends */
				_e.time_to_unstake = next_epoch_start_time + c.cpu_rental_epoch_length_seconds - (60 * 60 * 24 * 3);
				_e.cpu_wallet = next_cpu_contract;
				_e.wax_bucket = s.wax_available_for_rentals;
				_e.wax_to_refund = ZERO_WAX;
				/* redemption starts at the end of the epoch, ends 48h later */
				_e.redemption_period_start_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds;
				_e.redemption_period_end_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds + c.redemption_period_length_seconds;
				_e.total_cpu_funds_returned = ZERO_WAX;
				_e.total_added_to_redemption_bucket = ZERO_WAX;
			});

		} else {
			//update epoch
			asset current_wax_bucket = next_epoch_itr->wax_bucket;
			current_wax_bucket.amount = safeAddInt64(current_wax_bucket.amount, s.wax_available_for_rentals.amount);
			epochs_t.modify(next_epoch_itr, get_self(), [&](auto &_e){
				_e.wax_bucket = current_wax_bucket;
			});
		}

		//reset it to 0
		s.wax_available_for_rentals = ZERO_WAX;
	}

	//update the next_stakeall_time
	s.next_stakeall_time += c.seconds_between_stakeall;

	states.set(s, _self);
}