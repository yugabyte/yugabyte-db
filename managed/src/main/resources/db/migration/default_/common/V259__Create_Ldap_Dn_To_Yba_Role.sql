create table if not exists ldap_dn_to_yba_role (
    uuid                uuid not null,
    customer_uuid       uuid not null,
    distinguished_name  varchar(255) not null,
    yba_role            varchar(15) not null,
    constraint          ck_user_role check(yba_role in ('ReadOnly', 'Admin', 'BackupAdmin')),
    constraint          uq_dn unique (distinguished_name),
    constraint          pk_ldap_dn_to_yba_role primary key (uuid)
)