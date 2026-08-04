-- Copyright (c) YugabyteDB, Inc.
-- Table for storing file collection metadata (collect-files API)

CREATE TABLE IF NOT EXISTS file_collection (
    collection_uuid     UUID NOT NULL,
    customer_uuid       UUID NOT NULL,
    universe_uuid       UUID NOT NULL,
    node_tar_paths      TEXT NOT NULL,
    node_addresses      TEXT NOT NULL,
    status              VARCHAR(32) NOT NULL DEFAULT 'COLLECTED',
    created_at          TIMESTAMP NOT NULL,
    downloaded_at       TIMESTAMP,
    output_path         VARCHAR(1024),
    CONSTRAINT pk_file_collection PRIMARY KEY (collection_uuid),
    CONSTRAINT fk_file_collection_customer_uuid FOREIGN KEY (customer_uuid) 
        REFERENCES customer(uuid) ON DELETE CASCADE,
    CONSTRAINT fk_file_collection_universe_uuid FOREIGN KEY (universe_uuid) 
        REFERENCES universe(universe_uuid) ON DELETE CASCADE,
    CHECK (status IN ('COLLECTED', 'DOWNLOADING', 'DOWNLOADED', 'DB_NODES_CLEANED', 'YBA_CLEANED', 'CLEANED_UP', 'FAILED'))
);

-- Index for efficient lookup by universe
CREATE INDEX ix_file_collection_universe_uuid ON file_collection (universe_uuid);
