SELECT prfname, prfmaxfailedloginattempts, prfpasswordlocktime FROM pg_yb_profile;
SELECT rolprfrole, rolprfprofile, rolprfstatus, rolprffailedloginattempts FROM pg_yb_role_profile;
