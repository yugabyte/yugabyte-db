
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONSUM(__CORE_SCHEMA__.bson)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_sum_avg_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_sum_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_sum_avg_combine,
    mstype = bytea,
    MSFUNC = __API_CATALOG_SCHEMA__.bson_sum_avg_transition,
    MFINALFUNC = __API_CATALOG_SCHEMA__.bson_sum_final,
    MINVFUNC = helio_api_internal.bson_sum_avg_minvtransition,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONAVERAGE(__CORE_SCHEMA__.bson)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_sum_avg_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_avg_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_sum_avg_combine,
    mstype = bytea,
    MSFUNC = __API_CATALOG_SCHEMA__.bson_sum_avg_transition,
    MFINALFUNC = __API_CATALOG_SCHEMA__.bson_avg_final,
    MINVFUNC = helio_api_internal.bson_sum_avg_minvtransition,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONMAX(__CORE_SCHEMA__.bson)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_max_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_min_max_final,
    stype = __CORE_SCHEMA__.bson,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_max_combine,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONMIN(__CORE_SCHEMA__.bson)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_min_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_min_max_final,
    stype = __CORE_SCHEMA__.bson,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_min_combine,
    PARALLEL = SAFE
);


/*
* __API_CATALOG_SCHEMA__.bsonFIRST and __API_CATALOG_SCHEMA__.bsonLAST are the custom aggregation for first() and
* last() accumulators when sorting/ordering operation can be pushed
* down to the worker nodes.
*
* Worker nodes will apply the transition function on their share of
* the data to generate one partial AGGREGATE __API_CATALOG_SCHEMA__.per group per worker.
* The coordinator will then combine the partial aggregates using
* the combine function. Finally the finalfunc will be called on the
* intermediate result for each group.
*
* Note that __API_CATALOG_SCHEMA__.bsonFIRST() and __API_CATALOG_SCHEMA__.bsonLAST() has a __API_CATALOG_SCHEMA__.bson[] as the second
* argument which is an array of sort order specs of the form
* document |-<> '{ "sorkeypath1":1}'. The worker nodes will use
* these sort keys to order the documents while applying the transition
* function.
*/
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONFIRST(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[])
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_first_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_first_last_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_first_combine,
    PARALLEL = SAFE
);

/*
* See summary of __CORE_SCHEMA__.bsonFIRST()
*/
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONLAST(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[])
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_last_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_first_last_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_last_combine,
    PARALLEL = SAFE
);

/*
* __CORE_SCHEMA__.bsonFIRSTONSORTED and __CORE_SCHEMA__.bsonLASTONSORTED are the custom aggregation
* functions for first() and last() accumulators when the input to
* the group by stage is pre-sorted.
*
* In this case the aggregation is not pushed down to the worker node.
* All the data will be pulled at the coordinator and the transition
* function will be called on that data.
*
* Note the missing COMBINEFUNC. Since the work will be done at the
* coordinator, we won't need that.
*/
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONFIRSTONSORTED(__CORE_SCHEMA__.bson)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_first_transition_on_sorted,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_first_last_final_on_sorted,
    stype = bytea,
    PARALLEL = SAFE
);

/*
* See summary of __CORE_SCHEMA__.bsonFIRSTONSORTED()
*/
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONLASTONSORTED(__CORE_SCHEMA__.bson)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_last_transition_on_sorted,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_first_last_final_on_sorted,
    stype = bytea,
    PARALLEL = SAFE
);

/*
* __CORE_SCHEMA__.bsonFIRSTN and __CORE_SCHEMA__.bsonLASTN are the custom aggregation for firstN() and
* lastN() accumulators when sorting/ordering operation can be pushed
* down to the worker nodes.
*
* Worker nodes will apply the transition function on their share of
* the data to generate one partial AGGREGATE __API_CATALOG_SCHEMA__.per group per worker.
* The coordinator will then combine the partial aggregates using
* the combine function. Finally the finalfunc will be called on the
* intermediate result for each group.
*
* Second argument is the number of results to return or 'n'.
*
* Note that __CORE_SCHEMA__.bsonFIRSTN() and __CORE_SCHEMA__.bsonLASTN() has a __CORE_SCHEMA__.bson[] as the third
* argument which is an array of sort order specs of the form
* document | -<> '{ "sorkeypath1":1}'. The worker nodes will use
* these sort keys to order the documents while applying the transition
* function.
*/
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONFIRSTN(__CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[])
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_firstn_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_firstn_lastn_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_firstn_combine,
    PARALLEL = SAFE
);

/*
* See summary of __CORE_SCHEMA__.bsonFIRSTN()
*/
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONLASTN(__CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[])
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_lastn_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_firstn_lastn_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_lastn_combine,
    PARALLEL = SAFE
);

/*
* __CORE_SCHEMA__.bsonFIRSTNONSORTED and __CORE_SCHEMA__.bsonLASTNONSORTED are the custom aggregation
* functions for firstn() and lastn() accumulators when the input to
* the group by stage is pre-sorted.
*
* In this case the aggregation is not pushed down to the worker node.
* All the data will be pulled at the coordinator and the transition
* function will be called on that data.
*
* Note the missing COMBINEFUNC. Since the work will be done at the
* coordinator, we won\'t need that.
*/
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONFIRSTNONSORTED(__CORE_SCHEMA__.bson, bigint)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_firstn_transition_on_sorted,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_firstn_lastn_final_on_sorted,
    stype = bytea,
    PARALLEL = SAFE
);

/*
* See summary of __CORE_SCHEMA__.bsonFIRSTNONSORTED()
*/
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSONLASTNONSORTED(__CORE_SCHEMA__.bson, bigint)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_lastn_transition_on_sorted,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_firstn_lastn_final_on_sorted,
    stype = bytea,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSON_ARRAY_AGG(__CORE_SCHEMA__.bson, text)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_array_agg_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_array_agg_final,
    stype = bytea,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSON_OBJECT_AGG(__CORE_SCHEMA__.bson)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_object_agg_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_object_agg_final,
    stype = bytea,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSON_OUT(__CORE_SCHEMA__.bson, text, text, text, text)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_out_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_out_final,
    stype = bytea
);

/*
 * Implementation of the bson_array_agg aggregator with the addition of a boolean
 * field that indicates whether to treat { "": value } as an object or value.
 */
CREATE OR REPLACE AGGREGATE __API_CATALOG_SCHEMA__.BSON_ARRAY_AGG(__CORE_SCHEMA__.bson, text, boolean)
(
    SFUNC = __API_CATALOG_SCHEMA__.bson_array_agg_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_array_agg_final,
    stype = bytea,
    mstype = bytea,
    MSFUNC = __API_CATALOG_SCHEMA__.bson_array_agg_transition,
    MFINALFUNC = __API_CATALOG_SCHEMA__.bson_array_agg_final,
    MINVFUNC = helio_api_internal.bson_array_agg_minvtransition,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE helio_api_internal.BSON_ADD_TO_SET(helio_core.bson)
(
    SFUNC = helio_api_internal.bson_add_to_set_transition,
    FINALFUNC = helio_api_internal.bson_add_to_set_final,
    stype = bytea,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE helio_api_internal.BSON_MERGE_OBJECTS_ON_SORTED(helio_core.bson)
(
    SFUNC = helio_api_internal.bson_merge_objects_transition_on_sorted,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_object_agg_final,
    stype = bytea,
    PARALLEL = SAFE
);

/*
 * This can't use helio_core.bson due to citus type checks. We can migrate once the underlying tuples use the new types.
 */
CREATE OR REPLACE AGGREGATE helio_api_internal.BSON_MERGE_OBJECTS(__CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_merge_objects_transition,
    FINALFUNC = helio_api_internal.bson_merge_objects_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_firstn_combine,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE helio_api_internal.BSONSTDDEVPOP(__CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_std_dev_pop_samp_transition,
    FINALFUNC =  helio_api_internal.bson_std_dev_pop_final,
    stype = bytea,
    COMBINEFUNC = helio_api_internal.bson_std_dev_pop_samp_combine,
    PARALLEL = SAFE
);

CREATE OR REPLACE AGGREGATE helio_api_internal.BSONSTDDEVSAMP(__CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_std_dev_pop_samp_transition,
    FINALFUNC = helio_api_internal.bson_std_dev_samp_final,
    stype = bytea,
    COMBINEFUNC = helio_api_internal.bson_std_dev_pop_samp_combine,
    PARALLEL = SAFE
);

/*
 * Additional argument to BSONFIRST corresponding to input expression for $top operator  
 */
CREATE OR REPLACE AGGREGATE helio_api_internal.BSONFIRST(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_first_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_first_last_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_first_combine,
    PARALLEL = SAFE
);

/*
 * Additional argument to BSONLAST corresponding to input expression for $bottom operator  
 */
CREATE OR REPLACE AGGREGATE helio_api_internal.BSONLAST(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_last_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_first_last_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_last_combine,
    PARALLEL = SAFE
);

/*
 * Additional argument to BSONFIRSTN corresponding to input expression for $topN operator  
 */
CREATE OR REPLACE AGGREGATE helio_api_internal.BSONFIRSTN(__CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_firstn_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_firstn_lastn_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_firstn_combine,
    PARALLEL = SAFE
);

/*
 * Additional argument to BSONLASTN corresponding to input expression for $bottomN operator  
 */
CREATE OR REPLACE AGGREGATE helio_api_internal.BSONLASTN(__CORE_SCHEMA__.bson, bigint, __CORE_SCHEMA__.bson[], __CORE_SCHEMA__.bson)
(
    SFUNC = helio_api_internal.bson_lastn_transition,
    FINALFUNC = __API_CATALOG_SCHEMA__.bson_firstn_lastn_final,
    stype = bytea,
    COMBINEFUNC = __API_CATALOG_SCHEMA__.bson_lastn_combine,
    PARALLEL = SAFE
);
