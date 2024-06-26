/*
Description: This function is use in $merge stage aggregation pipeline to perform action in case of a match.
Arguments:
  - @bson: source document
  - @bson: target document
  - @int_parameter: action to perform in case of match e.g, replace, keepExisting, merge, fail, pipeline

Returns: 
  @bson: The final output to writing into target collection.
*/
CREATE OR REPLACE FUNCTION helio_api_internal.bson_dollar_merge_handle_when_matched(__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bson, int4)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_dollar_merge_handle_when_matched$function$;

/*
Description:  * The runtime implementation of this is identical to $eq. so we just make a direct function call to the function.
We just have a tail end argument of the index path so that we can do index pushdown for $merge scenarios.
Arguments:
  - @bson: target document
  - @bson: filter document
  - @text: index path

Returns: 
  @bool : True if target document matches the filter document.
*/
CREATE OR REPLACE FUNCTION helio_api_internal.bson_dollar_merge_join(__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bson, text)
 RETURNS BOOLEAN
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_merge_join_filter$function$;

/*
Description: This function adds object ID in a bson document if it not present.
Arguments:
  - @bson: input document
Returns: 
  @bson: output bson with object id.
*/
CREATE OR REPLACE FUNCTION helio_api_internal.bson_dollar_merge_add_object_id(__CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_dollar_merge_add_object_id$function$;

/*
Description: This function is use in $merge stage aggregation to fail in case of user wants to fail in `whenNotMatced`.
    This function accepts dummy arguments and has return type to prevent PostgreSQL from treating it as a constant function and evaluating it prematurely.
*/
CREATE OR REPLACE FUNCTION helio_api_internal.bson_dollar_merge_fail_when_not_matched(__CORE_SCHEMA__.bson,text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_dollar_merge_fail_when_not_matched$function$;