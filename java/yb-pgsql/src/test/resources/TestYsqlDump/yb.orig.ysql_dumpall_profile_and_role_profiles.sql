-- Set password encryption to MD5 for deterministic password hashes in tests
SET password_encryption = 'md5';

CREATE PROFILE profile_3_failed LIMIT FAILED_LOGIN_ATTEMPTS 3;
CREATE PROFILE profile_2_failed LIMIT FAILED_LOGIN_ATTEMPTS 2;

CREATE ROLE test_user WITH LOGIN PASSWORD 'secret';
CREATE ROLE test_user2 WITH LOGIN PASSWORD 'secret';
CREATE ROLE test_user3 WITH LOGIN PASSWORD 'secret';

ALTER ROLE test_user PROFILE profile_3_failed;
ALTER ROLE test_user2 PROFILE profile_3_failed;
ALTER ROLE test_user3 PROFILE profile_2_failed;

SET yb_non_ddl_txn_for_sys_tables_allowed = true;

-- Simulate 4 failed login attempts for test_user.
UPDATE pg_catalog.pg_yb_role_profile
SET rolprfstatus = 'l',
    rolprffailedloginattempts = 4
WHERE rolprfrole = (SELECT oid FROM pg_roles WHERE rolname = 'test_user')
  AND rolprfprofile = (SELECT oid FROM pg_yb_profile WHERE prfname = 'profile_3_failed');

-- Simulate 1 failed login attempts for test_user3.
WITH test_user3_oid AS (
    SELECT oid AS rol_oid FROM pg_roles WHERE rolname = 'test_user3'
),
profile2_oid AS (
    SELECT oid AS prf_oid FROM pg_yb_profile WHERE prfname = 'profile_2_failed'
)
UPDATE pg_catalog.pg_yb_role_profile
SET rolprfstatus = 'o',
    rolprffailedloginattempts = 1
WHERE rolprfrole = (SELECT rol_oid FROM test_user3_oid)
  AND rolprfprofile = (SELECT prf_oid FROM profile2_oid);
