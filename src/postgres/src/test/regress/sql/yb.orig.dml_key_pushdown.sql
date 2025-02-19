--
-- KEY Pushdown Processing.
-- This file tests keys whose column ordering is different from their ordering in table.
-- Pushdown expression is in a different test.
--
-- Test key-ordering with small tables.
CREATE TABLE tab(c1 INT, c2 INT, c3 INT, c4 INT, c5 INT, c6 INT, c7 INT,
			 			 		 PRIMARY KEY (c2, c6, c5, c4));

INSERT INTO tab VALUES (1, 2, 3, 4, 5, 6, 7),
  (1, 2, 3, 41, 51, 61, 7), (1, 2, 3, 41, 51, 62, 7), (1, 2, 3, 41, 51, 63, 7),
  (1, 2, 3, 41, 52, 61, 7), (1, 2, 3, 41, 52, 62, 7), (1, 2, 3, 41, 52, 63, 7),
  (1, 2, 3, 41, 53, 61, 7), (1, 2, 3, 41, 53, 62, 7), (1, 2, 3, 41, 53, 63, 7),
  (1, 2, 3, 42, 51, 61, 7), (1, 2, 3, 42, 51, 62, 7), (1, 2, 3, 42, 51, 63, 7),
  (1, 2, 3, 42, 52, 61, 7), (1, 2, 3, 42, 52, 62, 7), (1, 2, 3, 42, 52, 63, 7),
  (1, 2, 3, 42, 53, 61, 7), (1, 2, 3, 42, 53, 62, 7), (1, 2, 3, 42, 53, 63, 7),
  (1, 2, 3, 43, 51, 61, 7), (1, 2, 3, 43, 51, 62, 7), (1, 2, 3, 43, 51, 63, 7),
  (1, 2, 3, 43, 52, 61, 7), (1, 2, 3, 43, 52, 62, 7), (1, 2, 3, 43, 52, 63, 7),
  (1, 2, 3, 43, 53, 61, 7), (1, 2, 3, 43, 53, 62, 7), (1, 2, 3, 43, 53, 63, 7);

SELECT * FROM tab WHERE c2 = 2 AND c6 = 6 AND c5 = 5 AND c4 = 4;

SELECT * FROM tab WHERE c2 = 2;

SELECT * FROM tab WHERE c2 = 2 AND c6 = 6;

SELECT * FROM tab WHERE c2 = 2 AND c5 = 5;

SELECT * FROM tab WHERE c2 = 2 AND c4 = 4;

SELECT * FROM tab WHERE c2 = 2 AND c6 = 6 AND c5 = 5;

SELECT * FROM tab WHERE c2 = 2 AND c6 = 6 AND c4 = 4;

SELECT * FROM tab WHERE c2 = 2 AND c5 = 5 AND c4 = 4;

-- Test correctness of value ordering.
SELECT * FROM tab WHERE c2 = 2 ORDER BY c4, c5, c6;

SELECT * FROM tab WHERE c2 = 2 ORDER BY c4, c5, c6 DESC;

SELECT * FROM tab WHERE c2 = 2 ORDER BY c4, c5 DESC, c6;

SELECT * FROM tab WHERE c2 = 2 ORDER BY c4, c5 DESC, c6 DESC;

SELECT * FROM tab WHERE c2 = 2 ORDER BY c4 DESC, c5, c6;

SELECT * FROM tab WHERE c2 = 2 ORDER BY c4 DESC, c5, c6 DESC;

SELECT * FROM tab WHERE c2 = 2 ORDER BY c4 DESC, c5 DESC, c6;

SELECT * FROM tab WHERE c2 = 2 ORDER BY c4 DESC, c5 DESC, c6 DESC;

-- Test Primary Key whose column-ordering is different from the table.
SELECT * FROM airports WHERE
			 iso_region = 'US-MN' AND
			 name = 'Battle Lake Municipal Airport' AND
			 type = 'small_airport' AND
			 ident = '00MN';

SELECT * FROM airports WHERE
			 ident = '00MN' AND
			 name = 'Battle Lake Municipal Airport' AND
			 type = 'small_airport' AND
			 iso_region = 'US-MN';

SELECT * FROM airports WHERE
			 iso_region = 'US-MN' AND
			 type = 'heliport'
			 ORDER BY ident;

SELECT * FROM airports WHERE
			 iso_region = 'US-MN' AND
			 name = 'Battle Lake Municipal Airport';

SELECT * FROM airports WHERE
			 iso_region = 'US-MN' AND
			 ident = '00MN';

SELECT * FROM airports WHERE
			 iso_region = 'US-MN' AND
			 name = 'Battle Lake Municipal Airport' AND
			 type = 'small_airport';

SELECT * FROM airports WHERE
			 iso_region = 'US-MN' AND
			 name = 'Battle Lake Municipal Airport' AND
			 ident = '00MN';

SELECT * FROM airports WHERE
			 iso_region = 'US-MN' AND
			 type = 'small_airport' AND
			 ident = '00MN';

-- Test Secondary Key whose column ordering is different from the table.
SELECT * FROM airports WHERE
			 iso_region = 'US-MS' AND type = 'heliport' AND
			 coordinates = '-88.55889892578125, 30.53030014038086' AND
			 ident = '10MS' AND
			 name = 'Daniel Emergency Response Team Heliport';

SELECT * FROM airports WHERE
			 iso_region = 'US-MS' AND type = 'heliport'
			 ORDER BY ident;

SELECT * FROM airports WHERE
			 iso_region = 'US-MS' AND type = 'heliport' AND
			 coordinates = '-88.55889892578125, 30.53030014038086';

SELECT * FROM airports WHERE
			 iso_region = 'US-MS' AND type = 'heliport' AND
			 coordinates = '-88.55889892578125, 30.53030014038086' AND
			 ident = '10MS';

SELECT * FROM airports WHERE
			 iso_region = 'US-MS' AND type = 'heliport' AND
			 coordinates = '-88.55889892578125, 30.53030014038086' AND
			 name = 'Daniel Emergency Response Team Heliport';
