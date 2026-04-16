CREATE OR REPLACE AGGREGATE __API_SCHEMA_INTERNAL_V2__.BSONCOVARIANCEPOP(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
(
    SFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_transition,
    FINALFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_final,
    MSFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_transition,
    MFINALFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_final,
    MINVFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_invtransition,
    stype = bytea,
    mstype = bytea,
    COMBINEFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_combine,
    PARALLEL = SAFE
);


CREATE OR REPLACE AGGREGATE __API_SCHEMA_INTERNAL_V2__.BSONCOVARIANCESAMP(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
(
    SFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_transition,
    FINALFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_samp_final,
    MSFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_transition,
    MFINALFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_samp_final,
    MINVFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_invtransition,
    stype = bytea,
    mstype = bytea,
    COMBINEFUNC = __API_SCHEMA_INTERNAL_V2__.bson_covariance_pop_samp_combine,
    PARALLEL = SAFE
);


CREATE OR REPLACE AGGREGATE __API_SCHEMA_INTERNAL_V2__.BSONDERIVATIVE(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, bigint)
(
    SFUNC = __API_SCHEMA_INTERNAL_V2__.bson_derivative_transition,
    FINALFUNC = __API_SCHEMA_INTERNAL_V2__.bson_integral_derivative_final,
    stype = bytea,
    PARALLEL = SAFE
);


CREATE OR REPLACE AGGREGATE __API_SCHEMA_INTERNAL_V2__.BSONINTEGRAL(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, bigint)
(
    SFUNC = __API_SCHEMA_INTERNAL_V2__.bson_integral_transition,
    FINALFUNC = __API_SCHEMA_INTERNAL_V2__.bson_integral_derivative_final,
    stype = bytea,
    PARALLEL = SAFE
);