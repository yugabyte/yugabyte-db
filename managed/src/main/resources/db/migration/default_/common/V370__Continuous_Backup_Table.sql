
CREATE TABLE IF NOT EXISTS continuous_backup_config (
    uuid UUID NOT NULL,
    storage_config_uuid UUID NOT NULL,
    frequency BIGINT NOT NULL,
    frequency_time_unit VARCHAR(50) NOT NULL,
    num_backups_to_retain INTEGER NOT NULL,
    backup_dir VARCHAR(255) NOT NULL,
    CONSTRAINT pk_continuous_backup_config PRIMARY KEY (uuid),
    CONSTRAINT fk_storage_config_uuid FOREIGN KEY (storage_config_uuid) REFERENCES customer_config(config_uuid)
);
