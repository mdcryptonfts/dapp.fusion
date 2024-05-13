#pragma once

#define CONTRACT_NAME "fusion"
#define DEBUG true
#define mix64to128(a, b) (uint128_t(a) << 64 | uint128_t(b))

#include <eosio/eosio.hpp>
#include <eosio/print.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/symbol.hpp>
#include <string>
#include <eosio/crypto.hpp>
#include <eosio/transaction.hpp>
#include <eosio/singleton.hpp>
#include <eosio/binary_extension.hpp>
#include <eosio/producer_schedule.hpp>
#include<map>
#include "structs.hpp"
#include "constants.hpp"
#include "tables.hpp"

using namespace eosio;

CONTRACT fusion : public contract {
	public:
		using contract::contract;

		fusion(name receiver, name code, datastream<const char *> ds):
		contract(receiver, code, ds),
		configs(receiver, receiver.value),
		pol_state_s_2(POL_CONTRACT, POL_CONTRACT.value),
		states(receiver, receiver.value),
		state_s_2(receiver, receiver.value),
		state_s_3(receiver, receiver.value),
		top21_s(receiver, receiver.value)
		{}		

		//Main Actions
		ACTION addadmin(const eosio::name& admin_to_add);
		ACTION addcpucntrct(const eosio::name& contract_to_add);
		ACTION claimaslswax(const eosio::name& user, const eosio::asset& expected_output, const double& max_slippage);
		ACTION claimgbmvote(const eosio::name& cpu_contract);
		ACTION claimrefunds();
		ACTION claimrewards(const eosio::name& user);
		ACTION claimswax(const eosio::name& user);
		ACTION clearexpired(const eosio::name& user);
		ACTION createfarms();
		ACTION distribute();
		ACTION initconfig();
		ACTION initstate2();
		ACTION initstate3();
		ACTION inittop21();
		ACTION instaredeem(const eosio::name& user, const eosio::asset& swax_to_redeem);
		ACTION liquify(const eosio::name& user, const eosio::asset& quantity);
		ACTION liquifyexact(const eosio::name& user, const eosio::asset& quantity, 
			const eosio::asset& expected_output, const double& max_slippage);
		ACTION reallocate();
		ACTION redeem(const eosio::name& user);
		ACTION removeadmin(const eosio::name& admin_to_remove);
		ACTION reqredeem(const eosio::name& user, const eosio::asset& swax_to_redeem, const bool& accept_replacing_prev_requests);
		ACTION rmvcpucntrct(const eosio::name& contract_to_remove);
		ACTION rmvincentive(const uint64_t& poolId);
		ACTION setfallback(const eosio::name& caller, const eosio::name& receiver);
		ACTION setincentive(const uint64_t& poolId, const eosio::symbol& symbol_to_incentivize, const eosio::name& contract_to_incentivize, const double& parts_to_allocate);
		ACTION setpolshare(const double& pol_share);
		ACTION setrentprice(const eosio::name& caller, const eosio::asset& cost_to_rent_1_wax);
		ACTION stake(const eosio::name& user);
		ACTION stakeallcpu();
		ACTION sync(const eosio::name& caller);
		ACTION synctvl(const eosio::name& caller);
		ACTION unstakecpu();
		ACTION updatetop21();

		//Notifications
		[[eosio::on_notify("*::transfer")]] void receive_token_transfer(name from, name to, eosio::asset quantity, std::string memo);


	private:

		//Singletons
		config_singleton configs;
		pol_contract::state_singleton_2 pol_state_s_2;
		state_singleton states;
		state_singleton_2 state_s_2;
		state_singleton_3 state_s_3;
		top21_singleton top21_s;

		//Multi Index Tables
		alcor_contract::incentives_table incentives_t = alcor_contract::incentives_table(ALCOR_CONTRACT, ALCOR_CONTRACT.value);
		alcor_contract::pools_table pools_t = alcor_contract::pools_table(ALCOR_CONTRACT, ALCOR_CONTRACT.value);
		debug_table debug_t = debug_table(get_self(), get_self().value);
		epochs_table epochs_t = epochs_table(get_self(), get_self().value);
		lpfarms_table lpfarms_t = lpfarms_table(get_self(), get_self().value);
		snaps_table snaps_t = snaps_table(get_self(), get_self().value);
		producers_table _producers = producers_table(SYSTEM_CONTRACT, SYSTEM_CONTRACT.value);
		staker_table staker_t = staker_table(get_self(), get_self().value);
		state_snaps_table state_snaps_t = state_snaps_table(get_self(), get_self().value);


		//Functions
		void create_alcor_farm(const uint64_t& poolId, const eosio::symbol& token_symbol, const eosio::name& token_contract);
		void credit_total_claimable_wax(const eosio::asset& amount_to_credit);
		void debit_total_claimable_wax(const eosio::asset& amount_to_debit);
		void debit_user_redemptions_if_necessary(const name& user, const asset& swax_balance);
		std::string cpu_stake_memo(const eosio::name& cpu_receiver, const uint64_t& epoch_timestamp);
		std::vector<std::string> get_words(std::string memo);
		bool is_an_admin(const eosio::name& user);
		bool is_cpu_contract(const eosio::name& contract);
		void issue_lswax(const int64_t& amount, const eosio::name& receiver);
		void issue_swax(const int64_t& amount);
		bool memo_is_expected(const std::string& memo);
		uint64_t now();
		void retire_lswax(const int64_t& amount);
		void retire_swax(const int64_t& amount);
		void sync_epoch();
		void sync_tvl();
		void sync_user(const eosio::name& user);
		void transfer_tokens(const name& user, const asset& amount_to_send, const name& contract, const std::string& memo);
		void validate_token(const eosio::symbol& symbol, const eosio::name& contract);
		void zero_distribution();

		//Safemath
		double safeAddDouble(const double& a, const double& b);
		int64_t safeAddInt64(const int64_t& a, const int64_t& b);
		double safeDivDouble(const double& a, const double& b);
		double safeMulDouble(const double& a, const double& b);
		uint64_t safeMulUInt64(const uint64_t& a, const uint64_t& b);
		int64_t safeSubInt64(const int64_t& a, const int64_t& b);
};
