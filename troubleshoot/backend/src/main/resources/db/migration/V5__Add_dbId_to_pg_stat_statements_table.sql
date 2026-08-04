ALTER TABLE pg_stat_statements ADD COLUMN db_id varchar(1000) NOT NULL;
ALTER TABLE pg_stat_statements_query DROP CONSTRAINT pk_pss_query;
ALTER TABLE pg_stat_statements_query ADD CONSTRAINT pk_pss_query PRIMARY KEY(universe_id, db_id, query_id);
