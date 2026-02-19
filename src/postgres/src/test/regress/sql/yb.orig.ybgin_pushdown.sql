--
-- Yugabyte-owned test on ybgin index with expression pushdown
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun_ybgin.sql'
\i :filename

CREATE TABLE gin_pushdown(
    id varchar(64) not null,
    guid varchar(64) not null unique,
    status varchar(64),
    json_content jsonb not null,
    primary key (id)
);

-- gin index
CREATE INDEX gin_pushdown_json_content_idx ON gin_pushdown USING ybgin (json_content jsonb_path_ops);

INSERT INTO gin_pushdown
  SELECT x::text, x::text, x::text, ('{"refs": [{"val":"'||x||'"}, {"val":"'||x+1||'"}]}')::jsonb
  FROM generate_series (1, 10) x;

-- Find rows using gin index
SELECT $$
SELECT * FROM gin_pushdown WHERE json_content @> '{"refs": [{"val":"9"}]}'
$$ AS query \gset
:explain1run1

-- Use pushdown filter
SELECT $$
SELECT * FROM gin_pushdown WHERE json_content @> '{"refs": [{"val":"9"}]}' AND status <> '9'
$$ AS query \gset
:explain1run1
SELECT $$
SELECT * FROM gin_pushdown WHERE json_content @> '{"refs": [{"val":"9"}]}' AND status = '9'
$$ AS query \gset
:explain1run1

-- Pushdown filter that may seem to be pushed with the index scan, however ybgin index
-- does not store the indexed value, hence filter goes to the main relation
SELECT $$
SELECT * FROM gin_pushdown WHERE json_content @> '{"refs": [{"val":"9"}]}' AND json_content->'refs'->0->'val' <> '"9"'
$$ AS query \gset
:explain1run1
SELECT $$
SELECT * FROM gin_pushdown WHERE json_content @> '{"refs": [{"val":"9"}]}' AND json_content->'refs'->0->'val' = '"9"'
$$ AS query \gset
:explain1run1

-- Expression that does not refer any columns can go to the index.
SELECT $$
SELECT * FROM gin_pushdown WHERE json_content @> '{"refs": [{"val":"9"}]}' AND random() > 2.0
$$ AS query \gset
:explain1run1
SELECT $$
SELECT * FROM gin_pushdown WHERE json_content @> '{"refs": [{"val":"9"}]}' AND random() < 2.0
$$ AS query \gset
:explain1run1

-- Find row using regular index
SELECT $$
SELECT * FROM gin_pushdown WHERE guid = '9'
$$ AS query \gset
:explain1run1

-- Use pushdown filter
SELECT $$
SELECT * FROM gin_pushdown WHERE guid = '9' AND status <> '9'
$$ AS query \gset
:explain1run1
SELECT $$
SELECT * FROM gin_pushdown WHERE guid = '9' AND status = '9'
$$ AS query \gset
:explain1run1

-- Pushdown filter that goes with the index scan, since json_content is included
SELECT $$
SELECT * FROM gin_pushdown WHERE guid = '9' AND json_content->'refs'->0->'val' <> '"9"'
$$ AS query \gset
:explain1run1
SELECT $$
SELECT * FROM gin_pushdown WHERE guid = '9' AND json_content->'refs'->0->'val' = '"9"'
$$ AS query \gset
:explain1run1

-- Expression that does not refer any columns can go to the index.
SELECT $$
SELECT * FROM gin_pushdown WHERE guid = '9' AND random() > 2.0
$$ AS query \gset
:explain1run1
SELECT $$
SELECT * FROM gin_pushdown WHERE guid = '9' AND random() < 2.0
$$ AS query \gset
:explain1run1

-- Cleanup
DROP TABLE gin_pushdown;
