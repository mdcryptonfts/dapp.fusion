#pragma once


int64_t fusion::calculate_asset_share(const int64_t& quantity, const uint64_t& percentage){
	//percentage uses a scaling factor of 1e6
	//0.01% = 10,000
	//0.1% = 100,000
	//1% = 1,000,000
	//10% = 10,000,000
	//100% = 100,000,000

	//formula is ( quantity * percentage ) / ( 100 * SCALE_FACTOR_1E6 )
	if(quantity == 0) return 0;

	uint128_t result_128 = safeMulUInt128( (uint128_t) quantity, (uint128_t) percentage ) / safeMulUInt128( (uint128_t) 100, SCALE_FACTOR_1E6 );

  	return (int64_t) result_128;	
}

/** internal_get_swax_allocations
 *  used during distributions to determine how much of the rewards
 *  go to autocompounding, and to claimable wax for swax holders
 */

int64_t fusion::internal_get_swax_allocations( const int64_t& amount, const int64_t& swax_divisor, const int64_t& swax_supply ){
	//contract should have already validated quantity before calling this

	if( swax_divisor == 0 ) return 0;

	//formula is ( amount *  swax_divisor ) / swax_supply

	uint128_t result_128 = safeMulUInt128( (uint128_t) amount, (uint128_t) swax_divisor ) / (uint128_t) swax_supply;
	return (int64_t) result_128;	
}

int64_t fusion::internal_get_wax_owed_to_user(const int64_t& user_stake, const int64_t& total_stake, const int64_t& reward_pool){
	//user_stake, total_stake and reward_pool should have already been verified to be > 0
	//formula is ( user_stake * reward_pool ) / total_stake

	uint128_t result_128 = safeMulUInt128( (uint128_t) user_stake, (uint128_t) reward_pool ) / (uint128_t) total_stake;
	return (int64_t) result_128;
}

/** internal_liquify
 *  takes swax quantity as input
 *  then calculates the lswax output amount and returns it
 */

int64_t fusion::internal_liquify(const int64_t& quantity, state s){
	//contract should have already validated quantity before calling this

    /** need to account for initial period where the values are still 0
     *  also if lswax has not compounded yet, the result will be 1:1
     */
	
    if( (s.liquified_swax.amount == 0 && s.swax_currently_backing_lswax.amount == 0)
    	||
    	(s.liquified_swax.amount == s.swax_currently_backing_lswax.amount)
     ){
      return quantity;
    } else {
      	uint128_t result_128 = safeMulUInt128( (uint128_t) s.liquified_swax.amount, (uint128_t) quantity ) / (uint128_t) s.swax_currently_backing_lswax.amount;
      	return (int64_t) result_128;
    }		
}

int64_t fusion::internal_unliquify(const int64_t& quantity, state s){
	//contract should have already validated quantity before calling this
	
  	uint128_t result_128 = safeMulUInt128( (uint128_t) s.swax_currently_backing_lswax.amount, (uint128_t) quantity ) / (uint128_t) s.liquified_swax.amount;
  	return (int64_t) result_128;	
}