DO
$do$
BEGIN
	/*
	 * The role is a system-wide object which is not dropped if the extension
	 * is dropped. Therefore, if __API_SCHEMA__ api is repeatedly created and dropped,
	 * a regular CREATE ROLE would fail since __API_ADMIN_ROLE__ still exists.
	 * We therefore only create the role if it does not exist.
	 */
	IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = __SINGLE_QUOTED_STRING__(__API_ADMIN_ROLE__)) THEN
        CREATE ROLE __API_ADMIN_ROLE__;
    END IF;
END
$do$;
GRANT SELECT ON TABLE __API_CATALOG_SCHEMA__.collections TO public;
GRANT SELECT ON TABLE __API_CATALOG_SCHEMA__.collection_indexes TO public;

GRANT USAGE ON SCHEMA __API_CATALOG_SCHEMA__ TO __API_ADMIN_ROLE__;
GRANT USAGE ON SCHEMA __API_SCHEMA_INTERNAL__ TO __API_ADMIN_ROLE__;
GRANT USAGE ON SCHEMA __API_SCHEMA__ TO __API_ADMIN_ROLE__;
GRANT USAGE ON SCHEMA public TO __API_ADMIN_ROLE__;
GRANT ALL ON SCHEMA __API_DATA_SCHEMA__ TO __API_ADMIN_ROLE__;
GRANT ALL ON TABLE __API_CATALOG_SCHEMA__.collections TO __API_ADMIN_ROLE__;
GRANT ALL ON TABLE __API_CATALOG_SCHEMA__.collection_indexes TO __API_ADMIN_ROLE__;
GRANT ALL ON SEQUENCE __API_CATALOG_SCHEMA__.collections_collection_id_seq TO __API_ADMIN_ROLE__;
GRANT ALL ON SEQUENCE __API_CATALOG_SCHEMA__.collection_indexes_index_id_seq TO __API_ADMIN_ROLE__;