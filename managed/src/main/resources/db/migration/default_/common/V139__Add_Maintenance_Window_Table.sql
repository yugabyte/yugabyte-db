CREATE TABLE IF NOT EXISTS maintenance_window (
  uuid                             uuid primary key,
  customer_uuid                    uuid not null,
  name                             varchar(4000) not null,
  create_time                      timestamp not null,
  start_time                       timestamp not null,
  end_time                         timestamp not null,
  alert_configuration_filter       json_alias not null,
  applied_to_alert_configurations  boolean not null,
  constraint fk_mw_customer_uuid foreign key (customer_uuid) references customer (uuid)
    on delete cascade on update cascade
);

ALTER TABLE alert_configuration ADD COLUMN IF NOT EXISTS maintenance_window_uuids json_alias;

ALTER TABLE alert DROP CONSTRAINT IF EXISTS ck_alert_state;
ALTER TABLE alert ADD CONSTRAINT ck_alert_state
  CHECK (state in ('ACTIVE','SUSPENDED','ACKNOWLEDGED','RESOLVED'));
