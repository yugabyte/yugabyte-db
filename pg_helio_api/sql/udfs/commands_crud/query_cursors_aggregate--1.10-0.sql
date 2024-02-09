
CREATE OR REPLACE FUNCTION __API_SCHEMA__.aggregate_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA__.bson, OUT continuation __CORE_SCHEMA__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_aggregate_cursor_first_page$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA__.find_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA__.bson, OUT continuation __CORE_SCHEMA__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_find_cursor_first_page$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA__.list_collections_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA__.bson, OUT continuation __CORE_SCHEMA__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_list_collections_cursor_first_page$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA__.list_indexes_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA__.bson, OUT continuation __CORE_SCHEMA__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_list_indexes_cursor_first_page$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA__.cursor_get_more(
    database text, getMoreSpec __CORE_SCHEMA__.bson, continuationSpec __CORE_SCHEMA__.bson, OUT cursorPage __CORE_SCHEMA__.bson, OUT continuation __CORE_SCHEMA__.bson)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_cursor_get_more$function$;