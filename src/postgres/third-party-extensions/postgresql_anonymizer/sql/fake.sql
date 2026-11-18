-------------------------------------------------------------------------------
-- FAKE data
-------------------------------------------------------------------------------

SET search_path='';

-- We avoid using the floor() function in the function below because it is
-- way too slow. Instead we're using the mod operator like this:
--    (pg_catalog.random()*last_value)::INTEGER%last_value
-- See Issue #223 for more details
-- https://gitlab.com/dalibo/postgresql_anonymizer/-/merge_requests/223

CREATE OR REPLACE FUNCTION anon.fake_first_name()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.first_name_oid_seq
  )
  SELECT COALESCE(f.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.first_name f ON f.oid=r.oid;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_last_name()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.last_name_oid_seq
  )
  SELECT COALESCE(l.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.last_name l ON l.oid=r.oid;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_email()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.email_oid_seq
  )
  SELECT COALESCE(e.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.email e ON e.oid=r.oid;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_address()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.address_oid_seq
  )
  SELECT COALESCE(a.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.address a ON a.oid = r.oid
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_city()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.city_oid_seq
  )
  SELECT COALESCE(c.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.city c ON c.oid=r.oid;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_company()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.company_oid_seq
  )
  SELECT COALESCE(c.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.company c ON c.oid = r.oid
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_country()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.country_oid_seq
  )
  SELECT COALESCE(c.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.country c ON c.oid = r.oid
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_iban()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.iban_oid_seq
  )
  SELECT COALESCE(i.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.iban i ON i.oid = r.oid;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_postcode()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.postcode_oid_seq
  )
  SELECT COALESCE(p.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.postcode p ON p.oid = r.oid
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

CREATE OR REPLACE FUNCTION anon.fake_siret()
RETURNS TEXT AS $$
  WITH random AS (
    SELECT (pg_catalog.random()*last_value)::INTEGER%last_value+1 AS oid
    FROM anon.siret_oid_seq
  )
  SELECT COALESCE(s.val,anon.notice_if_not_init())
  FROM random r LEFT JOIN anon.siret s ON s.oid = r.oid;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

-- Lorem Ipsum
-- Usage:
--   `SELECT anon.lorem_ipsum()` returns 5 paragraphs
--   `SELECT anon.lorem_ipsum(2)` returns 2 paragraphs
--   `SELECT anon.lorem_ipsum( paragraph := 4 )` returns 4 paragraphs
--   `SELECT anon.lorem_ipsum( words := 20 )` returns 20 words
--   `SELECT anon.lorem_ipsum( characters := 7 )` returns 7 characters
--
CREATE OR REPLACE FUNCTION anon.lorem_ipsum(
  paragraphs INTEGER DEFAULT 5,
  words INTEGER DEFAULT 0,
  characters INTEGER DEFAULT 0
)
RETURNS TEXT AS $$
WITH
-- First let's shuffle the lorem_ipsum table
randomized_lorem_ipsum AS (
  SELECT *
  FROM anon.lorem_ipsum
  ORDER BY RANDOM()
),
-- if `characters` is defined,
-- then the limit is the number of characters
-- else return NULL
cte_characters AS (
  SELECT
    CASE characters
      WHEN 0
      THEN NULL
      ELSE substring( c.agg_paragraphs for characters )
    END AS n_characters
  FROM (
    SELECT string_agg(paragraph,E'\n') AS agg_paragraphs
    FROM randomized_lorem_ipsum
  ) AS c
),
-- if `characters` is not defined and if `words` defined,
-- then the limit is the number of words
-- else return NULL
cte_words AS (
  SELECT
    CASE words
      WHEN 0
      THEN NULL
      ELSE string_agg(w.unnested_words,' ')
    END AS n_words
  FROM (
    SELECT unnest(string_to_array(p.agg_paragraphs,' ')) as unnested_words
    FROM (
      SELECT string_agg(paragraph,E' \n') AS agg_paragraphs
      FROM randomized_lorem_ipsum
      ) AS p
    LIMIT words
  ) as w
),
-- if `characters` is notdefined and `words` is not defined,
-- then the limit is the number of paragraphs
cte_paragraphs AS (
  SELECT string_agg(l.paragraph,E'\n') AS n_paragraphs
  FROM (
    SELECT *
    FROM randomized_lorem_ipsum
    LIMIT paragraphs
  ) AS l
)
SELECT COALESCE(
  cte_characters.n_characters,
  cte_words.n_words,
  cte_paragraphs.n_paragraphs,
  anon.notice_if_not_init()
)
FROM
  cte_characters,
  cte_words,
  cte_paragraphs
;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL RESTRICTED -- because random
  SECURITY INVOKER
  SET search_path=''
;

RESET search_path;
