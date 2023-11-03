CREATE TABLE IF NOT EXISTS pitr_config(
    uuid                    UUID NOT NULL,
    name                    VARCHAR(255),
    universe_uuid           UUID NOT NULL,
    customer_uuid           UUID NOT NULL,
    table_type              VARCHAR(50) NOT NULL,
    db_name                 VARCHAR(255) NOT NULL,
    schedule_interval       BIGINT,
    retention_period        BIGINT,
    create_time             TIMESTAMP NOT NULL,
    update_time             TIMESTAMP,
    CONSTRAINT pk_pitr_config primary key (uuid),
    CONSTRAINT fk_pitr_config_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE,
    CONSTRAINT fk_pitr_config_universe_uuid FOREIGN KEY (universe_uuid) REFERENCES universe(universe_uuid) ON DELETE CASCADE,
    CONSTRAINT uq_pitr_config_per_universe UNIQUE (universe_uuid, table_type, db_name),
    CHECK ( table_type IN ('PGSQL_TABLE_TYPE', 'YQL_TABLE_TYPE'))
);
