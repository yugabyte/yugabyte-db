CREATE INDEX pss_graph_ix ON pg_stat_statements(universe_id, db_id, query_id, actual_timestamp DESC);
