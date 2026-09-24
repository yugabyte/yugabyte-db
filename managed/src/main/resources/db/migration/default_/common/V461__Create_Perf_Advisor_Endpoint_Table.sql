-- Copyright (c) YugabyteDB, Inc.

-- An external Perf Advisor a universe's collected data can be sent to in online mode. Owned by
-- YBA and pushed to each collector that needs it as that collector's export config, keyed by this
-- uuid - so the id is meaningful on both sides and must not be regenerated.
CREATE TABLE IF NOT EXISTS perf_advisor_endpoint (
    uuid                UUID          NOT NULL,
    customer_uuid       UUID          NOT NULL,
    name                VARCHAR(255)  NOT NULL,
    type                VARCHAR(20)   NOT NULL,
    metrics_endpoint    VARCHAR(1000) NOT NULL,
    metrics_type        VARCHAR(20)   NOT NULL,
    -- Encrypted at rest by Ebean's @Encrypted, so these arrive as pgp_sym_encrypt output rather
    -- than readable JSON.
    metrics_auth        bytea,
    collection_endpoint VARCHAR(1000) NOT NULL,
    collection_auth     bytea,
    ybm_account_id      VARCHAR(255),
    ybm_project_id      VARCHAR(255),
    create_time         TIMESTAMP     NOT NULL DEFAULT current_timestamp(0),
    update_time         TIMESTAMP     NOT NULL DEFAULT current_timestamp(0),
    CONSTRAINT pk_perf_advisor_endpoint PRIMARY KEY (uuid),
    CONSTRAINT fk_perf_advisor_endpoint_customer_uuid FOREIGN KEY (customer_uuid)
        REFERENCES customer (uuid) ON DELETE CASCADE,
    CONSTRAINT uq_perf_advisor_endpoint_name UNIQUE (customer_uuid, name)
);
