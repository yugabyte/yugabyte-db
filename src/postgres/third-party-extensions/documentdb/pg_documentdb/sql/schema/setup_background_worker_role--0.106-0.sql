/* Role to be used for background_worker database operations. */
DO
$do$
BEGIN
    /*
     * The role is a system-wide object which is not dropped if the extension
     * is dropped. Therefore, if __API_SCHEMA__ api is repeatedly created and dropped,
     * a regular CREATE ROLE would fail since __API_ADMIN_ROLE__ still exists.
     * We therefore only create the role if it does not exist.
     */
    IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = __SINGLE_QUOTED_STRING__(__API_BG_WORKER_ROLE__)) THEN
        CREATE ROLE __API_BG_WORKER_ROLE__ WITH LOGIN;
        GRANT __API_ADMIN_ROLE__ TO __API_BG_WORKER_ROLE__;
    END IF;
END
$do$;