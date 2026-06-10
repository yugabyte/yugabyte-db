CREATE ROLE test_role SUPERUSER;
CREATE SCHEMA test_role;
DROP SCHEMA test_role;
CREATE SCHEMA AUTHORIZATION test_role;
DROP SCHEMA test_role;
DROP ROLE test_role;

CREATE ROLE ddb$_test_role SUPERUSER;
CREATE SCHEMA ddb$_test_role; -- error
CREATE SCHEMA AUTHORIZATION ddb$_test_role; -- error
