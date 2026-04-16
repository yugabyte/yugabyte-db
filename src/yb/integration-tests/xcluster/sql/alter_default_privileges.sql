SET ROLE sandeep;
ALTER DEFAULT PRIVILEGES REVOKE select on tables from sandeep;
RESET ROLE;
ALTER DEFAULT PRIVILEGES REVOKE select on tables from sandeep;
