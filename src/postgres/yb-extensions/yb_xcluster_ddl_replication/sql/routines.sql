-- Check connections start in role not_automatic_mode.
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Check can override with every possible role.
SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'unspecified';
SELECT yb_xcluster_ddl_replication.get_replication_role();

SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'unavailable';
SELECT yb_xcluster_ddl_replication.get_replication_role();

SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'not_automatic_mode';
SELECT yb_xcluster_ddl_replication.get_replication_role();

SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'automatic_source';
SELECT yb_xcluster_ddl_replication.get_replication_role();

SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'automatic_target';
SELECT yb_xcluster_ddl_replication.get_replication_role();

-- Shortcuts for automatic roles.
SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'source';
SELECT yb_xcluster_ddl_replication.get_replication_role();

SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'target';
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Check for invalid roles.
SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'invalid';
SELECT yb_xcluster_ddl_replication.get_replication_role();



-- Check we can turn off override.
SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'source';
SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'none';
SELECT yb_xcluster_ddl_replication.get_replication_role();

SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'target';
SET yb_xcluster_ddl_replication.TEST_replication_role_override = '';
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Check override cannot be called if you are not superuser but
-- get_replication_role can.
SET SESSION AUTHORIZATION testuser;
SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'target';
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Check no select access to tables.
SELECT * FROM yb_xcluster_ddl_replication.ddl_queue;
