CREATE OR REPLACE AGGREGATE helio_api_internal.BSONCOVARIANCEPOP(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_covariance_pop_samp_transition,
    FINALFUNC = helio_api_internal.bson_covariance_pop_final,
    MSFUNC = helio_api_internal.bson_covariance_pop_samp_transition,
    MFINALFUNC = helio_api_internal.bson_covariance_pop_final,
    MINVFUNC = helio_api_internal.bson_covariance_pop_samp_invtransition,
    stype = bytea,
    mstype = bytea,
    COMBINEFUNC = helio_api_internal.bson_covariance_pop_samp_combine,
    PARALLEL = SAFE
);


CREATE OR REPLACE AGGREGATE helio_api_internal.BSONCOVARIANCESAMP(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_covariance_pop_samp_transition,
    FINALFUNC = helio_api_internal.bson_covariance_samp_final,
    MSFUNC = helio_api_internal.bson_covariance_pop_samp_transition,
    MFINALFUNC = helio_api_internal.bson_covariance_samp_final,
    MINVFUNC = helio_api_internal.bson_covariance_pop_samp_invtransition,
    stype = bytea,
    mstype = bytea,
    COMBINEFUNC = helio_api_internal.bson_covariance_pop_samp_combine,
    PARALLEL = SAFE
);