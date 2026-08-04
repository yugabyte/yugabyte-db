CREATE
    TABLE
        pg_stat_statements_query(
            scheduled_timestamp timestamptz NOT NULL,

            universe_id uuid NOT NULL,
            query_id bigint NOT NULL,

            db_id varchar(1000) NOT NULL,
            db_name varchar(4000) NOT NULL,
            query text NOT NULL,
            CONSTRAINT pk_pss_query PRIMARY KEY(universe_id, query_id)
        );
