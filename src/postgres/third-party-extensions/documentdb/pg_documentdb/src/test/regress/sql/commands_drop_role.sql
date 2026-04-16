SET documentdb.next_collection_id TO 1983100;
SET documentdb.next_collection_index_id TO 1983100;

SET documentdb.maxUserLimit TO 10;
\set VERBOSITY TERSE

-- Enable role CRUD operations for testing
SET documentdb.enableRoleCrud TO ON;

-- Enable db admin requirement for testing
SET documentdb.enableRolesAdminDBCheck TO ON;

-- ********* Test dropRole command basic functionality *********
-- Test dropRole of a custom role
SELECT documentdb_api.create_role('{"createRole":"custom_role", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT documentdb_api.drop_role('{"dropRole":"custom_role", "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname = 'custom_role';

-- Test dropRole of a referenced role which will still drop regardless
SELECT documentdb_api.create_role('{"createRole":"custom_role", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT documentdb_api.create_user('{"createUser":"userWithCustomRole", "pwd":"Valid$123Pass", "roles":[{"role":"readAnyDatabase","db":"admin"}], "$db":"admin"}');
GRANT "custom_role" TO "userWithCustomRole";
SELECT documentdb_api.drop_role('{"dropRole":"custom_role", "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname = 'custom_role';
SELECT documentdb_api.drop_user('{"dropUser":"userWithCustomRole", "$db":"admin"}');

-- Test dropRole with additional fields that should be ignored
SELECT documentdb_api.create_role('{"createRole":"custom_role", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT documentdb_api.drop_role('{"dropRole":"custom_role", "lsid":"test", "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname = 'custom_role';

-- ********* Test dropRole error inputs *********
-- Creating this custom_role for all negative tests below which will be removed at the end of all error tests
SELECT documentdb_api.create_role('{"createRole":"custom_role", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname = 'custom_role';

-- Test dropRole with missing dropRole field, should fail
SELECT documentdb_api.drop_role('{"$db":"admin"}');

-- Test dropRole with empty role name, should fail
SELECT documentdb_api.drop_role('{"dropRole":"", "$db":"admin"}');

-- Test dropRole with non-existent role, should fail
SELECT documentdb_api.drop_role('{"dropRole":"nonExistentRole", "$db":"admin"}');

-- Test dropRole with invalid JSON, should fail
SELECT documentdb_api.drop_role('{"dropRole":"invalidJson"');

-- Test dropRole with non-string role name, should fail
SELECT documentdb_api.drop_role('{"dropRole":1, "$db":"admin"}');

-- Test dropRole with null role name, should fail
SELECT documentdb_api.drop_role('{"dropRole":null, "$db":"admin"}');

-- Test dropping built-in roles, should fail
SELECT documentdb_api.drop_role('{"dropRole":"documentdb_admin_role", "$db":"admin"}');

-- Test dropRole with a non-admin database, should fail
SELECT documentdb_api.drop_role('{"dropRole":"custom_role", "lsid":"test", "$db":"nonAdminDatabase"}');

-- Test dropRole of a system role
SELECT documentdb_api.drop_role('{"dropRole":"documentdb_bg_worker_role", "$db":"admin"}');

-- Test dropRole of non-existing role with built-in role prefix, which should fail with role not found
SELECT documentdb_api.drop_role('{"dropRole":"documentdb_role", "$db":"admin"}');

-- Test dropRole with unsupported field, should fail
SELECT documentdb_api.drop_role('{"dropRole":"custom_role", "unsupportedField":"value", "$db":"admin"}');

-- Test dropRole with different casing which should fail with role not found
SELECT documentdb_api.drop_role('{"dropRole":"CUSTOM_ROLE", "$db":"admin"}');

-- Test dropRole with non-admin database, should fail
SELECT documentdb_api.drop_role('{"dropRole":"custom_role", "$db":"nonAdminDatabase"}');

-- Test dropRole with no database, should fail
SELECT documentdb_api.drop_role('{"dropRole":"custom_role"}');

-- Test dropRole when feature is disabled
SET documentdb.enableRoleCrud TO OFF;
SELECT documentdb_api.drop_role('{"dropRole":"custom_role", "$db":"admin"}');
SET documentdb.enableRoleCrud TO ON;

-- Test dropRole when admin DB check is disabled, should succeed
SET documentdb.enableRolesAdminDBCheck TO OFF;
SELECT documentdb_api.drop_role('{"dropRole":"custom_role", "$db":"nonAdminDatabase"}');
SELECT rolname FROM pg_roles WHERE rolname = 'custom_role';

-- Test dropRole with no database when admin DB check is disabled
SELECT documentdb_api.create_role('{"createRole":"custom_role", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT documentdb_api.drop_role('{"dropRole":"custom_role"}');
SELECT rolname FROM pg_roles WHERE rolname = 'custom_role';
SET documentdb.enableRolesAdminDBCheck TO ON;

-- Clean up and Reset settings
RESET documentdb.enableRoleCrud;
RESET documentdb.enableRolesAdminDBCheck;