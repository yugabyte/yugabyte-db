SET md5_password_warnings = off;
LOAD 'passwordcheck';

CREATE USER regress_passwordcheck_user1;
-- YB Note: In passwordcheck_extra, the default length is 15. However in passwordcheck there was
-- no check for max length. Increase max length to accomodate the tests in this file.
SET passwordcheck.maximum_length TO 30;

-- ok
-- YB Note: The test has changed from upstream because passwordcheck_extra expects
-- an upper case and a number by default.
ALTER USER regress_passwordcheck_user1 PASSWORD 'a_N1ce_long_password';

-- error: too short
ALTER USER regress_passwordcheck_user1 PASSWORD 'tooshrt';

-- ok
-- YB Note: passwordcheck_extra's equivalent of passwordcheck.min_password_length
-- is passwordcheck.minimum_length, and the password needs an upper case and a number.
SET passwordcheck.minimum_length = 6;
ALTER USER regress_passwordcheck_user1 PASSWORD 'v_Shr1';

-- error: contains user name
ALTER USER regress_passwordcheck_user1 PASSWORD 'xyzregress_passwordcheck_user1';

-- error: contains only letters
ALTER USER regress_passwordcheck_user1 PASSWORD 'alessnicelongpassword';

-- encrypted ok (password is "secret")
ALTER USER regress_passwordcheck_user1 PASSWORD 'md592350e12ac34e52dd598f90893bb3ae7';

-- error: password is user name
ALTER USER regress_passwordcheck_user1 PASSWORD 'md507a112732ed9f2087fa90b192d44e358';

DROP USER regress_passwordcheck_user1;
