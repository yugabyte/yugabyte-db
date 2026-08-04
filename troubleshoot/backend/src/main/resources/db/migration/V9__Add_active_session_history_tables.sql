CREATE
    TABLE
        active_session_history(
            sample_time timestamptz NOT NULL,
            universe_id UUID NOT NULL,
            node_name VARCHAR(1000) NOT NULL,
            root_request_id UUID NOT NULL,
            rpc_request_id BIGINT,
            wait_event_component TEXT NOT NULL,
            wait_event_class TEXT NOT NULL,
            wait_event TEXT NOT NULL,
            top_level_node_id UUID NOT NULL,
            query_id BIGINT NOT NULL,
            ysql_session_id BIGINT NOT NULL,
            client_node_ip TEXT,
            wait_event_aux TEXT,
            sample_weight REAL
        );

CREATE INDEX ash_universe_ix ON active_session_history(universe_id, sample_time DESC);
CREATE INDEX ash_query_ix ON active_session_history(universe_id, query_id, sample_time DESC);

CREATE
    TABLE
        active_session_history_query_state(
            universe_id UUID NOT NULL,
            node_name VARCHAR(1000) NOT NULL,
            last_sample_time timestamptz NOT NULL,
            CONSTRAINT pk_ash_query_state PRIMARY KEY(universe_id, node_name)
        );
