BEGIN;
CREATE TEMPORARY TABLE tmp_email
AS SELECT * FROM anon.email;
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
COMMIT;

