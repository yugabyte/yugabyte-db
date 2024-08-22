DO
$do$
BEGIN
	/*
	 * The role is a system-wide object which is not dropped if the extension
	 * is dropped. Therefore, if helioapi is repeatedly created and dropped,
	 * a regular CREATE ROLE would fail since __API_ADMIN_ROLE__ still exists.
	 * We therefore only create the role if it does not exist.
	 */
	IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'helio_readonly_role') THEN
        CREATE ROLE helio_readonly_role;
    END IF;
END
$do$;

GRANT USAGE ON SCHEMA __API_CATALOG_SCHEMA__ TO helio_readonly_role;
GRANT USAGE ON SCHEMA __API_SCHEMA_INTERNAL__ TO helio_readonly_role;
GRANT USAGE ON SCHEMA __API_SCHEMA__ TO helio_readonly_role;
GRANT USAGE ON SCHEMA __API_DATA_SCHEMA__ TO helio_readonly_role;
 
GRANT SELECT ON TABLE __API_CATALOG_SCHEMA__.collections TO helio_readonly_role;
GRANT SELECT ON TABLE __API_CATALOG_SCHEMA__.collection_indexes TO helio_readonly_role;