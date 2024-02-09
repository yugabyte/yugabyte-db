
CREATE OR REPLACE FUNCTION helio_api.aggregate_cursor_first_page(
    database text, commandSpec helio_core.bson, cursorId int8 DEFAULT 0, OUT cursorPage helio_core.bson, OUT continuation helio_core.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_aggregate_cursor_first_page$function$;

CREATE OR REPLACE FUNCTION helio_api.find_cursor_first_page(
    database text, commandSpec helio_core.bson, cursorId int8 DEFAULT 0, OUT cursorPage helio_core.bson, OUT continuation helio_core.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_find_cursor_first_page$function$;


CREATE OR REPLACE FUNCTION helio_api.list_collections_cursor_first_page(
    database text, commandSpec helio_core.bson, cursorId int8 DEFAULT 0, OUT cursorPage helio_core.bson, OUT continuation helio_core.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_list_collections_cursor_first_page$function$;

CREATE OR REPLACE FUNCTION helio_api.list_indexes_cursor_first_page(
    database text, commandSpec helio_core.bson, cursorId int8 DEFAULT 0, OUT cursorPage helio_core.bson, OUT continuation helio_core.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_list_indexes_cursor_first_page$function$;


CREATE OR REPLACE FUNCTION helio_api.cursor_get_more(
    database text, getMoreSpec helio_core.bson, continuationSpec helio_core.bson, OUT cursorPage helio_core.bson, OUT continuation helio_core.bson)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_cursor_get_more$function$;