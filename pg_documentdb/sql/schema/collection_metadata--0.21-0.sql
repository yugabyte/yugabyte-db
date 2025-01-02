CREATE OR REPLACE TRIGGER collections_trigger_validate_dbname
    BEFORE INSERT OR UPDATE ON __API_CATALOG_SCHEMA__.collections 
    FOR EACH ROW EXECUTE FUNCTION helio_api_internal.trigger_validate_dbname();