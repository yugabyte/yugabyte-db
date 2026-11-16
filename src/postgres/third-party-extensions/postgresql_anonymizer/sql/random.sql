--
-- # Random Generic Data
--
-- These random functions generate numeric/date values without
-- accessing any dictionary.
--

SET search_path = '';

-- sequenced id
CREATE SEQUENCE anon.random_id_seq CYCLE;

SELECT pg_catalog.setval('anon.random_id_seq', (9223372036854775807*pg_catalog.random())::BIGINT);

CREATE OR REPLACE FUNCTION anon.random_id()
RETURNS BIGINT AS $$
    SELECT pg_catalog.nextval('anon.random_id_seq');
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because nextval
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.random_id_int()
RETURNS INT AS $$
    SELECT (pg_catalog.nextval('anon.random_id_seq')%2147483647)::INT;
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because nextval
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.random_id_smallint()
RETURNS SMALLINT AS $$
    SELECT (pg_catalog.nextval('anon.random_id_seq')%32767)::SMALLINT;
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because nextval
  SECURITY INVOKER
  SET search_path=''
;

-- Date

CREATE OR REPLACE FUNCTION anon.random_date_between(
  date_start timestamp WITH TIME ZONE,
  date_end timestamp WITH TIME ZONE
)
RETURNS timestamp WITH TIME ZONE AS $$
    SELECT (random()*(date_end-date_start))::interval+date_start;
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;


-- The random functions below are written in Rust (see `src/random.rs`)

-- Undocumented and kept for backward compatibility with v1
-- returns an empty string when l=0
CREATE OR REPLACE FUNCTION anon.random_string(
  l integer
)
RETURNS text
AS $$
  SELECT anon.random_string(pg_catalog.int4range(l,l+1));
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

-- Undocumented and kept for backward compatibility with v1
CREATE OR REPLACE FUNCTION anon.random_phone(
  prefix TEXT
)
RETURNS TEXT AS $$
  SELECT  anon.random_number_with_format(prefix||'#########');
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

-- Undocumented and kept for backward compatibility with v1
CREATE OR REPLACE FUNCTION anon.plop(
  prefix TEXT
)
RETURNS TEXT AS $$
  SELECT  anon.random_number_with_format(prefix||'#########');
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

--
-- hashing a seed with a random salt
--
CREATE OR REPLACE FUNCTION anon.random_hash(
  seed TEXT
)
RETURNS TEXT AS
$$
  SELECT anon.digest(
    seed,
    anon.random_string(6),
    pg_catalog.current_setting('anon.algorithm')
  );
$$
  LANGUAGE SQL
  VOLATILE
  SECURITY DEFINER
  PARALLEL RESTRICTED -- because random
  SET search_path = ''
  RETURNS NULL ON NULL INPUT
;

-- Array
CREATE OR REPLACE FUNCTION anon.random_in(
  a ANYARRAY
)
RETURNS ANYELEMENT AS
$$
  SELECT a[pg_catalog.floor(pg_catalog.random()*array_length(a,1)+1)]
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;


-- ENUM

CREATE OR REPLACE FUNCTION anon.random_in_enum(
  element ANYELEMENT
)
RETURNS ANYELEMENT AS
$$
  SELECT anon.random_in(enum_range(element));
$$
  LANGUAGE SQL
  VOLATILE
  -- We need to invoke the function like this anon.random_in_enum(NULL::CARD);
  --RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.random_in_daterange(
  r DATERANGE
)
RETURNS DATE
AS $$
  SELECT CAST(
      (
        pg_catalog.random()
        *(pg_catalog.upper(r)::TIMESTAMP-pg_catalog.lower(r)::TIMESTAMP)
      )::INTERVAL
      +pg_catalog.lower(r)
      AS DATE
  );
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;


CREATE OR REPLACE FUNCTION anon.random_in_tsrange(
  r TSRANGE
)
RETURNS TIMESTAMP WITHOUT TIME ZONE
AS $$
  SELECT CAST(
    (
      pg_catalog.random()
      *(pg_catalog.upper(r)-pg_catalog.lower(r))
    )::INTERVAL
    +pg_catalog.lower(r)
    AS TIMESTAMP WITHOUT TIME ZONE);
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.random_in_tstzrange(
  r TSTZRANGE
)
RETURNS TIMESTAMP WITH TIME ZONE
AS $$
  SELECT CAST(
    (
      pg_catalog.random()
      *(pg_catalog.upper(r)-pg_catalog.lower(r))
    )::INTERVAL
    +pg_catalog.lower(r)
    AS TIMESTAMP WITH TIME ZONE);
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

RESET search_path;
