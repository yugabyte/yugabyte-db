/*
Description: This function is use in $merge stage aggregation pipeline to perform action in case of a match.
Arguments:
  - @bson: source document
  - @bson: target document
  - @int_parameter: action to perform in case of match e.g, replace, keepExisting, merge, fail, pipeline

Returns: 
  @bson: The final output to writing into target collection.
*/
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_handle_when_matched(__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bson, int4)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_merge_handle_when_matched$function$;

/*
Description: support function for index pushdown in merge join used by __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_join
*/
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_filter_support(internal)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $$bson_dollar_merge_filter_support$$;


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
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_join(__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bson, text)
 RETURNS BOOLEAN
 LANGUAGE c
 SUPPORT __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_filter_support
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_merge_join_filter$function$;

/*
Description: This function adds object ID in a bson document if it not present.
Arguments:
  - @bson: input document
  - @bson: object id to add
Returns: 
  @bson: output bson with object id.
*/
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_add_object_id(__CORE_SCHEMA__.bson,  __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_merge_add_object_id$function$;

/*
Description: This function is use in $merge stage aggregation to fail in case of user wants to fail in `whenNotMatced`.
    This function accepts dummy arguments and has return type to prevent PostgreSQL from treating it as a constant function and evaluating it prematurely.
*/
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_fail_when_not_matched(__CORE_SCHEMA__.bson,text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_merge_fail_when_not_matched$function$;

/*
Description: This function generates object ID.
Arguments:
  - @bson: this is a dummy input as Non Immutable function are not supported in citus MERGE command.
           passing dummy input to prevent PostgreSQL from treating it as a constant function and evaluating it prematurely.
Returns: 
  @bson: output bson with object id.
*/
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_generate_object_id(__CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_merge_generate_object_id$function$;

/*
Description: This function extracts merge filter from source document to match against target document.
Arguments:
  - @bson: source document 
  - @text: path to extract filter from source document
Returns: 
  @bson: extracted filter document.
*/
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_extract_merge_filter(__CORE_SCHEMA__.bson, text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_extract_merge_filter$function$;