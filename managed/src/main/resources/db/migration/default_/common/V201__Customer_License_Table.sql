-- Copyright (c) YugaByte, Inc.

create table customer_license (
    license_uuid              uuid not null,
    customer_uuid             uuid not null,
    license_type              varchar(30) not null,
    license                   text not null,
    creation_date             TIMESTAMP DEFAULT NOW() NOT NULL,
    constraint pk_customer_license primary key (license_uuid),
    constraint fk_cl_customer_uuid foreign key (customer_uuid) references customer (uuid)
        on delete cascade on update cascade
);
