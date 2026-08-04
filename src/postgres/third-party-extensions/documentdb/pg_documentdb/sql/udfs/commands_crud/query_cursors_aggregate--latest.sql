-- CODESYNC: See PostProcessCursorPage at src/commands/cursors.c when modifying the OUT variables
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.aggregate_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA_V2__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_aggregate_cursor_first_page$function$;

-- CODESYNC: See PostProcessCursorPage at src/commands/cursors.c when modifying the OUT variables
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.find_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA_V2__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_find_cursor_first_page$function$;

-- CODESYNC: See PostProcessCursorPage at src/commands/cursors.c when modifying the OUT variables
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.list_collections_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA_V2__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_list_collections_cursor_first_page$function$;

-- CODESYNC: See PostProcessCursorPage at src/commands/cursors.c when modifying the OUT variables
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.list_indexes_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA_V2__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_list_indexes_cursor_first_page$function$;

-- CODESYNC: See PostProcessCursorPage at src/commands/cursors.c when modifying the OUT variables
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.cursor_get_more(
    database text, getMoreSpec __CORE_SCHEMA_V2__.bson, continuationSpec __CORE_SCHEMA_V2__.bson, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_cursor_get_more$function$;