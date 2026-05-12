
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.rum_extract_tsquery(tsquery,internal,smallint,internal,internal,internal,internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'documentdb_rum_extract_tsquery'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.rum_tsquery_consistent(internal, smallint, tsvector, integer, internal, internal, internal, internal)
RETURNS bool
AS 'MODULE_PATHNAME', 'documentdb_rum_tsquery_consistent'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.rum_tsvector_config(internal)
RETURNS void
AS 'MODULE_PATHNAME', 'documentdb_rum_tsvector_config'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.rum_tsquery_pre_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal)
RETURNS bool
AS 'MODULE_PATHNAME', 'documentdb_rum_tsquery_pre_consistent'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.rum_tsquery_distance(internal,smallint,tsvector,int,internal,internal,internal,internal,internal)
RETURNS float8
AS 'MODULE_PATHNAME', 'documentdb_rum_tsquery_distance'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.rum_ts_join_pos(internal, internal)
RETURNS bytea
AS 'MODULE_PATHNAME', 'documentdb_rum_ts_join_pos'
LANGUAGE C IMMUTABLE STRICT;