-- Check connections start in role not_automatic_mode.
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Check can override with every possible role.
CALL yb_xcluster_ddl_replication.TEST_override_replication_role('unspecified');
SELECT yb_xcluster_ddl_replication.get_replication_role();

CALL yb_xcluster_ddl_replication.TEST_override_replication_role('unavailable');
SELECT yb_xcluster_ddl_replication.get_replication_role();

CALL yb_xcluster_ddl_replication.TEST_override_replication_role('not_automatic_mode');
SELECT yb_xcluster_ddl_replication.get_replication_role();

CALL yb_xcluster_ddl_replication.TEST_override_replication_role('automatic_source');
SELECT yb_xcluster_ddl_replication.get_replication_role();

CALL yb_xcluster_ddl_replication.TEST_override_replication_role('automatic_target');
SELECT yb_xcluster_ddl_replication.get_replication_role();

-- Shortcuts for automatic roles.
CALL yb_xcluster_ddl_replication.TEST_override_replication_role('source');
SELECT yb_xcluster_ddl_replication.get_replication_role();

CALL yb_xcluster_ddl_replication.TEST_override_replication_role('target');
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Check for invalid roles.
CALL yb_xcluster_ddl_replication.TEST_override_replication_role('invalid');
SELECT yb_xcluster_ddl_replication.get_replication_role();



-- Check we can turn off override.
CALL yb_xcluster_ddl_replication.TEST_override_replication_role('source');
CALL yb_xcluster_ddl_replication.TEST_override_replication_role('no_override');
SELECT yb_xcluster_ddl_replication.get_replication_role();

CALL yb_xcluster_ddl_replication.TEST_override_replication_role('target');
CALL yb_xcluster_ddl_replication.TEST_override_replication_role('');
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Check override cannot be called if you are not superuser but
-- get_replication_role can.
SET SESSION AUTHORIZATION testuser;
CALL yb_xcluster_ddl_replication.TEST_override_replication_role('target');
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Check no select access to tables.
SELECT * FROM yb_xcluster_ddl_replication.ddl_queue;
