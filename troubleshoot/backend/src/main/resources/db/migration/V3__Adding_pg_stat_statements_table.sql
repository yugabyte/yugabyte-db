CREATE
    TABLE
        pg_stat_statements(
            scheduled_timestamp timestamptz NOT NULL,
            actual_timestamp timestamptz NOT NULL,
            universe_id uuid NOT NULL,
            node_name varchar(1000) NOT NULL,

            query_id bigint NOT NULL,
            calls bigint NOT NULL,
            rows bigint NOT NULL,
            avg_latency double precision NOT NULL,
            mean_latency double precision,
            p90_latency double precision,
            p99_latency double precision,
            max_latency double precision
        );
