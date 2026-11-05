-- Copyright (c) YugabyteDB, Inc.

-- Idempotency-Key header support for mutating API operations (PLAT-19704). One row per key a
-- customer has used: the first request with a key claims the row (state IN_PROGRESS), and a
-- successful response is stored on it (state COMPLETED) so retries with the same key replay that
-- response instead of running the operation again. Rows expire after the retention period and are
-- removed by a periodic cleanup.
CREATE TABLE IF NOT EXISTS api_idempotency_key (
    uuid                  UUID          NOT NULL,
    customer_uuid         UUID          NOT NULL,
    idempotency_key       VARCHAR(255)  NOT NULL,
    user_uuid             UUID,
    request_method        VARCHAR(16)   NOT NULL,
    request_path          VARCHAR(2048) NOT NULL,
    -- SHA-256 of the method, path and canonical request body, to reject a key reused for a
    -- different request.
    request_fingerprint   VARCHAR(64)   NOT NULL,
    state                 VARCHAR(16)   NOT NULL,
    response_status       INTEGER,
    response_content_type VARCHAR(255),
    -- Encrypted at rest by Ebean's @Encrypted: responses can carry generated secrets.
    response_body         bytea,
    create_time           TIMESTAMP     NOT NULL,
    update_time           TIMESTAMP     NOT NULL,
    expire_time           TIMESTAMP     NOT NULL,
    CONSTRAINT pk_api_idempotency_key PRIMARY KEY (uuid),
    CONSTRAINT fk_api_idempotency_key_customer_uuid FOREIGN KEY (customer_uuid)
        REFERENCES customer (uuid) ON DELETE CASCADE,
    CONSTRAINT uq_api_idempotency_key_customer_key UNIQUE (customer_uuid, idempotency_key)
);

CREATE INDEX IF NOT EXISTS ix_api_idempotency_key_expire_time
    ON api_idempotency_key (expire_time);
