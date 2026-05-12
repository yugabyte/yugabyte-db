SET documentdb.next_collection_id TO 1982900;
SET documentdb.next_collection_index_id TO 1982900;

SET documentdb.maxUserLimit TO 10;
\set VERBOSITY TERSE

-- Test createRole command
-- Enable role CRUD operations for testing
SET documentdb.enableRoleCrud TO ON;

-- Enable db admin requirement for testing
SET documentdb.enableRolesAdminDBCheck TO ON;

-- Test creating a basic role that inherits from readAnyDatabase
SELECT documentdb_api.create_role('{"createRole":"customReadRole", "roles":["documentdb_readonly_role"], "$db":"admin"}');

-- Verify the role was created
SELECT rolname FROM pg_roles WHERE rolname = 'customReadRole';

-- Test creating a role that inherits from admin role
SELECT documentdb_api.create_role('{"createRole":"customAdminRole", "roles":["documentdb_admin_role"], "$db":"admin"}');

-- Verify the role was created
SELECT rolname FROM pg_roles WHERE rolname = 'customAdminRole';

-- Test creating a role that inherits from multiple roles
SELECT documentdb_api.create_role('{"createRole":"multiInheritRole", "roles":["documentdb_readonly_role", "documentdb_admin_role"], "$db":"admin"}');

-- Verify the role was created
SELECT rolname FROM pg_roles WHERE rolname = 'multiInheritRole';

-- Verify the role has both inherited roles
SELECT r2.rolname as inherited_role 
FROM pg_auth_members am 
JOIN pg_roles r1 ON am.member = r1.oid 
JOIN pg_roles r2 ON am.roleid = r2.oid 
WHERE r1.rolname = 'multiInheritRole' 
ORDER BY r2.rolname;

-- Test createRole with no roles array
SELECT documentdb_api.create_role('{"createRole":"noRolesRole", "$db":"admin"}');

-- Test createRole with empty roles array
SELECT documentdb_api.create_role('{"createRole":"emptyRolesRole", "roles":[], "$db":"admin"}');

-- Test error cases
-- Test createRole with empty role name, should fail
SELECT documentdb_api.create_role('{"createRole":"", "roles":["documentdb_readonly_role"], "$db":"admin"}');

-- Test createRole with invalid inherited role, should fail
SELECT documentdb_api.create_role('{"createRole":"invalidInheritRole", "roles":["nonexistent_role"], "$db":"admin"}');

-- Test createRole with invalid roles array type, should fail
SELECT documentdb_api.create_role('{"createRole":"invalidRolesType", "roles":"not_an_array", "$db":"admin"}');

-- Test createRole with non-string role names in array, should fail
SELECT documentdb_api.create_role('{"createRole":"invalidRoleNames", "roles":[123, true], "$db":"admin"}');

-- Test createRole with missing createRole field, should fail
SELECT documentdb_api.create_role('{"roles":["documentdb_readonly_role"], "$db":"admin"}');

-- Test createRole with a built-in role, should fail
SELECT documentdb_api.create_role('{"createRole": "documentdb_admin_role", "$db":"admin"}');

-- Test createRole with unsupported field, should fail
SELECT documentdb_api.create_role('{"createRole":"unsupportedFieldRole", "roles":["documentdb_readonly_role"], "unsupportedField":"value", "$db":"admin"}');

-- Test creating role with same name as existing role, should fail
SELECT documentdb_api.create_role('{"createRole":"customReadRole", "roles":["documentdb_readonly_role"], "$db":"admin"}');

-- Test roles array with mixed valid and invalid roles, should fail
SELECT documentdb_api.create_role('{"createRole":"mixedRolesTest", "roles":["documentdb_readonly_role", "invalid_role"], "$db":"admin"}');

-- Test invalid JSON in createRole, should fail
SELECT documentdb_api.create_role('{"createRole":"invalidJson", "roles":["documentdb_readonly_role"');

-- Test createRole with non-admin database, should fail
SELECT documentdb_api.create_role('{"createRole":"nonAdminDatabaseRole", "roles":["documentdb_readonly_role"], "$db":"nonAdminDatabase"}');

-- Test createRole with no database, should fail
SELECT documentdb_api.create_role('{"createRole":"noDatabaseRole", "roles":["documentdb_readonly_role"]}');

-- Test role functionality by creating users and assigning custom roles
-- Create a user first
SELECT documentdb_api.create_user('{"createUser":"testRoleUser", "pwd":"Valid$123Pass", "roles":[{"role":"readAnyDatabase","db":"admin"}], "$db":"admin"}');

-- Grant custom role to user (this demonstrates the role can be granted)
GRANT "customReadRole" TO "testRoleUser";

-- Verify the grant worked by checking pg_auth_members
SELECT r1.rolname as member_role, r2.rolname as granted_role 
FROM pg_auth_members am 
JOIN pg_roles r1 ON am.member = r1.oid 
JOIN pg_roles r2 ON am.roleid = r2.oid 
WHERE r1.rolname = 'testRoleUser' AND r2.rolname = 'customReadRole';

-- Test that role inheritance works correctly
-- Check that multiInheritRole has both inherited roles
SELECT r1.rolname as member_role, r2.rolname as granted_role 
FROM pg_auth_members am 
JOIN pg_roles r1 ON am.member = r1.oid 
JOIN pg_roles r2 ON am.roleid = r2.oid 
WHERE r1.rolname = 'multiInheritRole' 
ORDER BY r2.rolname;

-- Test edge cases for role names
-- Test role name with maximum length (63 characters is PostgreSQL limit)
SELECT documentdb_api.create_role('{"createRole":"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname = 'abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk';

-- Test role name exceeding maximum length (64 characters), will be truncated to 63 characters
SELECT documentdb_api.create_role('{"createRole":"1abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname = '1abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghij';

-- Test createRole when feature is disabled
SET documentdb.enableRoleCrud TO OFF;
SELECT documentdb_api.create_role('{"createRole":"disabledFeatureRole", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SET documentdb.enableRoleCrud TO ON;

-- Test createRole when admin DB check is disabled
SET documentdb.enableRolesAdminDBCheck TO OFF;
SELECT documentdb_api.create_role('{"createRole":"nonAdminDBNoCheckRole", "roles":["documentdb_readonly_role"], "$db":"nonAdminDatabase"}');
SELECT rolname FROM pg_roles WHERE rolname = 'nonAdminDBNoCheckRole';

-- Test createRole with no $db field
SELECT documentdb_api.create_role('{"createRole":"noDbFieldRole", "roles":["documentdb_readonly_role"]}');
SELECT rolname FROM pg_roles WHERE rolname = 'noDbFieldRole';
SET documentdb.enableRolesAdminDBCheck TO ON;

-- Test special characters in role names
SELECT documentdb_api.create_role('{"createRole":"role_with_underscores", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT documentdb_api.create_role('{"createRole":"role-with-dashes", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT documentdb_api.create_role('{"createRole":"role123numbers", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname IN ('role_with_underscores', 'role-with-dashes', 'role123numbers') ORDER BY rolname;

-- Test case sensitivity in createRole
SELECT documentdb_api.create_role('{"createRole":"CaseSensitiveRole", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT documentdb_api.create_role('{"createRole":"casesensitiverole", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname IN ('CaseSensitiveRole', 'casesensitiverole') ORDER BY rolname;

-- Test createRole with additional fields that should be ignored
SELECT documentdb_api.create_role('{"createRole":"ignoredFieldsRole", "roles":["documentdb_readonly_role"], "lsid":"session123", "$db":"admin"}');
SELECT rolname FROM pg_roles WHERE rolname = 'ignoredFieldsRole';

-- Clean up test roles
DROP ROLE IF EXISTS "customReadRole";
DROP ROLE IF EXISTS "customAdminRole";
DROP ROLE IF EXISTS "multiInheritRole";
DROP ROLE IF EXISTS "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk";
DROP ROLE IF EXISTS "1abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghij";
DROP ROLE IF EXISTS "role_with_underscores";
DROP ROLE IF EXISTS "role-with-dashes";
DROP ROLE IF EXISTS "role123numbers";
DROP ROLE IF EXISTS "CaseSensitiveRole";
DROP ROLE IF EXISTS "casesensitiverole";
DROP ROLE IF EXISTS "ignoredFieldsRole";
DROP ROLE IF EXISTS "nonAdminDBNoCheckRole";
DROP ROLE IF EXISTS "noDbFieldRole";

-- Clean up test users
SELECT documentdb_api.drop_user('{"dropUser":"testRoleUser", "$db":"admin"}');

-- Test createRole with blocked role names, should fail
SET documentdb.blockedRolePrefixList TO 'block,test';
SELECT documentdb_api.create_role('{"createRole":"block", "roles":["documentdb_readonly_role"], "$db":"admin"}');
SELECT documentdb_api.create_role('{"createRole":"test_block_user", "roles":["documentdb_readonly_role"], "$db":"admin"}');
RESET documentdb.blockedRolePrefixList;

-- Reset settings
RESET documentdb.enableRoleCrud;
RESET documentdb.blockedRolePrefixList;
RESET documentdb.enableRolesAdminDBCheck;