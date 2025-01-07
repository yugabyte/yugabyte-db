
-- swap out the proc for the operator '=?=' to be the one in helio_api.
UPDATE pg_catalog.pg_operator
    SET oprcode = (SELECT pro.oid FROM pg_proc pro JOIN pg_namespace nmp ON pro.pronamespace = nmp.oid WHERE pro.proname = 'bson_unique_index_equal' AND nmp.nspname = 'helio_api_internal')
    WHERE oprname = '=?=' AND oprnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'helio_api_catalog' );