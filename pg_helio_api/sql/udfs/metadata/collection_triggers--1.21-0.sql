/* Trigger function to validate dbname before a create collection */
CREATE OR REPLACE FUNCTION helio_api_internal.trigger_validate_dbname()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
BEGIN
	PERFORM helio_api_internal.validate_dbname(NEW.database_name);
	RETURN NEW;
END;
$function$;

/* Function to validate dbname in the collecitons catalog table */
CREATE OR REPLACE FUNCTION helio_api_internal.validate_dbname(dbname text)
 RETURNS void
 LANGUAGE c
AS 'MODULE_PATHNAME', $$validate_dbname$$;
