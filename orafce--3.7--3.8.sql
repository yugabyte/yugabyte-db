-- bugfixes
CREATE OR REPLACE FUNCTION oracle.nvl(int, int)
RETURNS int AS $$
SELECT coalesce($1, $2)
$$ LANGUAGE sql IMMUTABLE;

-- this routine was renamed on PostgreSQL 12, so the wrapping should be
-- more complex and more dynamic.
CREATE OR REPLACE FUNCTION varchar2_transform(internal)
RETURNS internal
AS 'MODULE_PATHNAME','orafce_varchar_transform'
LANGUAGE C
STRICT
IMMUTABLE;

CREATE OR REPLACE FUNCTION nvarchar2_transform(internal)
RETURNS internal
AS 'MODULE_PATHNAME','orafce_varchar_transform'
LANGUAGE C
STRICT
IMMUTABLE;
