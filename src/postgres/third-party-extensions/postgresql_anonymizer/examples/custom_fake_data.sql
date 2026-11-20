--
-- How to add custom fake data
--
-- Let's say we want to add more fake emails
--

BEGIN;

CREATE EXTENSION IF NOT EXISTS anon;

SELECT anon.init();

-- We don't have enough fake emails:w
SELECT COUNT(*) FROM anon.email;

-- copy the email table
CREATE TEMPORARY TABLE tmp_email
AS SELECT * FROM anon.email;

-- generate additional values based on the current ones

TRUNCATE anon.email;
INSERT INTO anon.email
SELECT
  ROW_NUMBER() OVER (),
  concat(u.username,'@', d.domain)
FROM
(
  SELECT split_part(address,'@',1) AS username
  FROM tmp_email
  ORDER BY RANDOM()
  LIMIT 10
) u,
(
  SELECT split_part(address,'@',2) AS domain
  FROM tmp_email
  ORDER BY RANDOM()
  LIMIT 5
) d
;

SELECT COUNT(*) FROM anon.email;

ROLLBACK;