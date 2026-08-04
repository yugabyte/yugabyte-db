--
-- Validate yb_db_admin can reassign object ownership
--
CREATE ROLE regress_user1;
CREATE ROLE regress_user2;
CREATE ROLE regress_user3;
CREATE ROLE non_sys_superuser SUPERUSER;
GRANT regress_user1 TO regress_user3;
GRANT regress_user2 TO regress_user3;
SET SESSION AUTHORIZATION yb_db_admin;
REASSIGN OWNED BY regress_user1 TO regress_user2;
REASSIGN OWNED BY regress_user2 TO regress_user1;
-- should fail, cannot reassign objects owned by superuser
REASSIGN OWNED BY postgres TO yb_db_admin;
-- should fail, assigning from superuser to non-superuser
REASSIGN OWNED BY non_sys_superuser TO yb_db_admin;
-- should fail, user needs privileges on both old and new role
SET SESSION AUTHORIZATION regress_user1;
REASSIGN OWNED BY regress_user1 TO regress_user2;
REASSIGN OWNED BY regress_user2 TO regress_user1;
-- should succeed, user has privileges on both old and new role
SET SESSION AUTHORIZATION regress_user3;
REASSIGN OWNED BY regress_user1 TO regress_user2;
REASSIGN OWNED BY regress_user2 TO regress_user1;
