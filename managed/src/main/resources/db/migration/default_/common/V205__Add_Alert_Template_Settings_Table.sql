create table if not exists alert_template_settings(
  uuid                             uuid primary key,
  customer_uuid                    uuid not null,
  template                         varchar(100) not null,
  create_time                      timestamp not null,
  labels                           json_alias not null,
  constraint uq_ats_customer_template unique (customer_uuid, template),
  constraint fk_ats_customer_uuid foreign key (customer_uuid) references customer (uuid)
    on delete cascade on update cascade
);
