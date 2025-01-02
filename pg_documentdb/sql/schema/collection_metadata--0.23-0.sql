CREATE TRIGGER collections_trigger
 AFTER UPDATE OR DELETE ON helio_api_catalog.collections
 FOR STATEMENT EXECUTE FUNCTION helio_api_internal.collection_update_trigger();