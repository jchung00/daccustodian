
void daccustodian::distributePay() {
    custodians_table custodians(_self, _self);

    //Find the median pay using a temporary vector to hold the requestedpay amounts.
    std::vector<asset> reqpays;
    for (auto cust: custodians) {
        reqpays.push_back(cust.requestedpay);
    }

    // Using nth_element to just sort for the entry we need for the median value.
    size_t mid = reqpays.size() / 2;
    std::nth_element(reqpays.begin(), reqpays.begin() + mid, reqpays.end());

    asset medianAsset = reqpays[mid];

    for (auto cust: custodians) {
        pending_pay.emplace(_self, [&](pay &p) {
            p.key = pending_pay.available_primary_key();
            p.receiver = cust.cust_name;
            p.quantity = medianAsset;
            p.memo = "Custodian pay. Thank you.";
        });
    }

    print("distribute pay");
}

void daccustodian::assertPeriodTime() {
    uint32_t timestamp = now();
    uint32_t periodBlockCount = timestamp - _currentState.lastperiodtime;
    eosio_assert(periodBlockCount > configs().periodlength,
                 "New period is being called too soon. Wait until the period has completed.");
}

void daccustodian::allocateCustodians(bool early_election) {

    eosio::print("Configure custodians for the next period.");

    custodians_table custodians(_self, _self);
    auto byvotes = registered_candidates.get_index<N(byvotesrank)>();
    auto cand_itr = byvotes.begin();

    int32_t electcount = configs().numelected;
    uint8_t currentCustodianCount = 0;

    if (!early_election) {
        eosio::print("Empty the custodians table to get a full set of new custodians based on the current votes.");
        auto cust_itr = custodians.begin();
        while (cust_itr != custodians.end()) {
            const auto &reg_candidate = registered_candidates.get(cust_itr->cust_name, "Corrupt data: Trying to set a lockup delay on candidate leaving office.");
            registered_candidates.modify(reg_candidate, cust_itr->cust_name, [&](candidate &c) {
                eosio::print("Lockup stake for release delay.");
                c.custodian_end_time_stamp = now() + configs().lockup_release_time_delay;
            });
            cust_itr = custodians.erase(cust_itr);
        }
    }

    eosio::print("Select only enough candidates to fill the gaps.");
    for (auto itr = custodians.begin(); itr != custodians.end(); itr++) { ++currentCustodianCount; }

    while (currentCustodianCount < electcount) {
        if (cand_itr == byvotes.end() || cand_itr->total_votes == 0) {
            eosio::print("The pool of eligible candidates has been exhausted");
            return;
        }

        //  If the candidate is inactive or is already a custodian skip to the next one.
        if (!cand_itr->is_active || custodians.find(cand_itr->candidate_name) != custodians.end()) {
            cand_itr++;
        } else {
            custodians.emplace(_self, [&](custodian &c) {
                c.cust_name = cand_itr->candidate_name;
                c.requestedpay = cand_itr->requestedpay;
                c.total_votes = cand_itr->total_votes;
            });

            byvotes.modify(cand_itr, cand_itr->candidate_name, [&](candidate &c) {
                    eosio::print("Lockup stake for release delay.");
                    c.custodian_end_time_stamp = now() + configs().lockup_release_time_delay;
            });

            currentCustodianCount++;
            cand_itr++;
        }
    }
}

void daccustodian::setCustodianAuths() {

    custodians_table custodians(_self, _self);

    account_name accountToChange = configs().authaccount;

    vector<eosiosystem::permission_level_weight> accounts;

    for (auto it = custodians.begin(); it != custodians.end(); it++) {
        eosiosystem::permission_level_weight account{
                .permission = eosio::permission_level(it->cust_name, N(active)),
                .weight = (uint16_t) 1,
        };
        accounts.push_back(account);
    }

    eosiosystem::authority high_contract_authority{
            .threshold = configs().auth_threshold_high,
            .keys = {},
            .accounts = accounts
    };

    action(permission_level{accountToChange,
                            N(owner)},
           N(eosio), N(updateauth),
           std::make_tuple(
                   accountToChange,
                   HIGH_PERMISSION,
                   N(owner),
                   high_contract_authority))
            .send();

    eosiosystem::authority medium_contract_authority{
            .threshold = configs().auth_threshold_mid,
            .keys = {},
            .accounts = accounts
    };

    action(permission_level{accountToChange, N(owner)},
           N(eosio), N(updateauth),
           std::make_tuple(
                   accountToChange,
                   MEDIUM_PERMISSION,
                   N(owner),
                   medium_contract_authority))
            .send();

    eosiosystem::authority low_contract_authority{
            .threshold = configs().auth_threshold_low,
            .keys = {},
            .accounts = accounts
    };

    action(permission_level{accountToChange, N(owner)},
           N(eosio), N(updateauth),
           std::make_tuple(
                   accountToChange,
                   LOW_PERMISSION,
                   N(owner),
                   low_contract_authority))
            .send();

    eosiosystem::authority one_contract_authority{
            .threshold = 1,
            .keys = {},
            .accounts = accounts
    };

    action(permission_level{accountToChange, N(owner)},
           N(eosio), N(updateauth),
           std::make_tuple(
                   accountToChange,
                   ONE_PERMISSION,
                   N(owner),
                   one_contract_authority))
            .send();
}

void daccustodian::newperiod(string message) {

    assertPeriodTime();

    contr_config config = configs();

    // Get the max supply of the lockup asset token (eg. EOSDAC)
    auto tokenStats = stats(eosio::string_to_name(TOKEN_CONTRACT), config.lockupasset.symbol.name()).begin();
    uint64_t max_supply = tokenStats->supply.amount;

    double percent_of_current_voter_engagement =
            double(_currentState.total_weight_of_votes) / double(max_supply) * 100.0;

    eosio::print("\n\nToken max supply: ", max_supply, " total votes so far: ", _currentState.total_weight_of_votes);
    eosio::print("\n\nNeed inital engagement of: ", config.initial_vote_quorum_percent, "% to start the DAC.");
    eosio::print("\n\nToken supply: ", max_supply * 0.0001, " total votes so far: ", _currentState.total_weight_of_votes * 0.0001);
    eosio::print("\n\nNeed initial engagement of: ", config.initial_vote_quorum_percent, "% to start the DAC.");
    eosio::print("\n\nNeed ongoing engagement of: ", config.vote_quorum_percent,
                 "% to allow new periods to trigger after initial activation.");
    eosio::print("\n\nPercent of current voter engagement: ", percent_of_current_voter_engagement, "\n\n");

    eosio_assert(_currentState.met_initial_votes_threshold == true ||
                 percent_of_current_voter_engagement > config.initial_vote_quorum_percent,
                 "Voter engagement is insufficient to activate the DAC.");
    _currentState.met_initial_votes_threshold = true;

    eosio_assert(percent_of_current_voter_engagement > config.vote_quorum_percent,
                 "Voter engagement is insufficient to process a new period");

    // Distribute pay to the current custodians.
    distributePay();

    // Set custodians for the next period.
    allocateCustodians(false);

    // Set the auths on the dacauthority account
    setCustodianAuths();

    _currentState.lastperiodtime = now();


//        Schedule the the next election cycle at the end of the period.
//        transaction nextTrans{};
//        nextTrans.actions.emplace_back(permission_level(_self,N(active)), _self, N(newperiod), std::make_tuple("", false));
//        nextTrans.delay_sec = configs().periodlength;
//        nextTrans.send(N(newperiod), false);
}