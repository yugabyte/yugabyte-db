ALTER TABLE users ADD COLUMN user_type varchar(5) default 'local';
ALTER TABLE users ADD COLUMN ldap_specified_role boolean default FALSE;
