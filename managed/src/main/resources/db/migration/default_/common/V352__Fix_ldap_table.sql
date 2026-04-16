ALTER TABLE ldap_dn_to_yba_role
DROP CONSTRAINT ck_user_role;

ALTER TABLE ldap_dn_to_yba_role
ADD CONSTRAINT ck_user_role check(yba_role in ('ReadOnly', 'Admin', 'BackupAdmin', 'ConnectOnly'));
