CREATE OR REPLACE TRIGGER collections_trigger_validate_dbname
    BEFORE INSERT OR UPDATE ON __API_CATALOG_SCHEMA__.collections 
    FOR EACH ROW EXECUTE FUNCTION __API_SCHEMA_INTERNAL_V2__.trigger_validate_dbname();