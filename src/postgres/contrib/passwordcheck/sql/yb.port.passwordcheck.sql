LOAD 'passwordcheck';
CREATE USER regress_user1;
-- YB Note: In passwordcheck_extra, the default length is 15. However in passwordcheck there was
-- no check for max length. Increase max length to accomodate the tests in this file.
SET passwordcheck.maximum_length TO 30;

-- ok
-- YB Note: The test has changed from upstream because passwordcheck_extra expects
-- an upper case and a number by default.
ALTER USER regress_user1 PASSWORD 'a_N1ce_long_password';

-- error: too short
ALTER USER regress_user1 PASSWORD 'tooshrt';

-- error: contains user name
ALTER USER regress_user1 PASSWORD 'xyzregress_user1';

-- error: contains only letters
ALTER USER regress_user1 PASSWORD 'alessnicelongpassword';

-- encrypted ok (password is "secret")
ALTER USER regress_user1 PASSWORD 'md51a44d829a20a23eac686d9f0d258af13';

-- error: password is user name
ALTER USER regress_user1 PASSWORD 'md5e589150ae7d28f93333afae92b36ef48';

-- YB: SCRAM secrets too, as a pg_dumpall script replays them
ALTER USER regress_user1 PASSWORD 'SCRAM-SHA-256$4096:eWItcmVncmVzcy1zYWx0IQ==$1u+NSwBmQ2sI2tROw57JW0jT2oTy8xoL81985X/Vg5Y=:2ZkNYmIzpykurJaUthlcQtl0hC1AsG+DMig4z4p3glA='; -- YB: ok (password is "secret")
ALTER USER regress_user1 PASSWORD 'SCRAM-SHA-256$4096:eWItcmVncmVzcy1zYWx0IQ==$1Tnlet73stV3gHFDoXaTQWwmH7EatQLKeN1tVEW1QVs=:1Cw4KCJILJ66ej5C/ZHjn6LLFFlOAevbPawDllwYrmU='; -- YB: error, password is user name
DROP USER regress_user1;
