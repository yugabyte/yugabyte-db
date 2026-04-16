/* Trigger function to validate dbname before a create collection */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.trigger_validate_dbname()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
BEGIN
	PERFORM __API_SCHEMA_INTERNAL_V2__.validate_dbname(NEW.database_name);
	RETURN NEW;
END;
$function$;

/* Function to validate dbname in the collecitons catalog table */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.validate_dbname(dbname text)
 RETURNS void
 LANGUAGE c
AS 'MODULE_PATHNAME', $$validate_dbname$$;
