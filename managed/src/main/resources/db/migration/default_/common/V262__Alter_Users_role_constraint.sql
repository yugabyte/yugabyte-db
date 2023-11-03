ALTER TABLE users
	DROP CONSTRAINT ck_users_role;
ALTER TABLE users
	ADD CONSTRAINT ck_users_role check (role in ('ConnectOnly', 'ReadOnly','Admin','SuperAdmin', 'BackupAdmin'));