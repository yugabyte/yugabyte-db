CREATE
    TABLE
        pg_stat_statements(
            scheduled_timestamp timestamptz NOT NULL,
            actual_timestamp timestamptz NOT NULL,
            customer_id uuid NOT NULL,
            universe_id uuid NOT NULL,
            cluster_id uuid NOT NULL,
            node_name varchar(1000) NOT NULL,
            cloud varchar(100) NOT NULL,
            region varchar(100) NOT NULL,
            az varchar(100) NOT NULL,

            db_id varchar(1000) NOT NULL,
            db_name varchar(4000) NOT NULL,
            query_id bigint NOT NULL,
            query text NOT NULL,
            calls bigint NOT NULL,
            total_time double precision NOT NULL,
            rows bigint NOT NULL,
            yb_latency_histogram jsonb
        );
