--
-- This file contains various experiments to implement the 
-- dynamic masking engine using the RULE system.
--
-- The main issue is that PostgreSQL considers that a VIEW 
-- is an empty TABLE and a RULE. Therefore it does not allow
-- user to put a RULE on a non-empty TABLE :
-- 
-- The examples below will return the following error:
-- « could not convert table x to a view because it is not empty »
--
--

BEGIN;

CREATE UNLOGGED TABLE t1 (
	id SERIAL,
	credit_card TEXT
);

CREATE UNLOGGED TABLE t2 (
    id SERIAL,
    credit_card TEXT
);

INSERT INTO t1("credit_card")
VALUES
('5467909664060024'),
('2346657632423434'),
('5897577324346790')
;

CREATE RULE "_RETURN" AS
ON SELECT TO t2
DO INSTEAD
	WITH anon AS (TABLE t1)
   	SELECT
		id,
		'xxx' AS credit_card
	FROM  anon
;

SELECT * FROM t2;


CREATE VIEW mask_dot_public_dot_t1 AS
SELECT id, md5(credit_card) AS credit_card
FROM public.t1;


-- ERROR:  event qualifications are not implemented for rules on SELECT
CREATE RULE "_RETURN" AS
ON SELECT TO t1
WHERE CURRENT_USER IN ('joe','alice') -- would be anon.mask_roles()
DO INSTEAD
SELECT * FROM mask_dot_public_dot_t1
;
