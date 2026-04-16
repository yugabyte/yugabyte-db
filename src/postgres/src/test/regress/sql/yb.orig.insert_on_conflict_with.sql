CREATE TABLE with_a (i int, PRIMARY KEY (i DESC));
CREATE TABLE with_b (i int, PRIMARY KEY (i ASC));
INSERT INTO with_a VALUES (generate_series(1, 10));
INSERT INTO with_b VALUES (generate_series(11, 20));

BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (generate_series(1, 11)) ON CONFLICT (i) DO NOTHING RETURNING i
) INSERT INTO with_b VALUES (generate_series(1, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
TABLE with_b;
ABORT;

BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (generate_series(1, 11)) ON CONFLICT (i) DO NOTHING RETURNING i
) INSERT INTO with_a VALUES (generate_series(1, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
ABORT;

BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (generate_series(6, 11)) ON CONFLICT (i) DO NOTHING RETURNING i
) INSERT INTO with_a VALUES (generate_series(10, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
ABORT;

BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (11) RETURNING i
) INSERT INTO with_a VALUES (generate_series(10, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
ABORT;

BEGIN;
WITH w(i) AS (
    DELETE FROM with_a WHERE i = 10 RETURNING i
) INSERT INTO with_a VALUES (generate_series(9, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
ABORT;

BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (generate_series(6, 11)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 100 RETURNING i
) INSERT INTO with_a SELECT CASE
    WHEN u < 12 THEN u
    WHEN u < 14 THEN -(u + (SELECT max(i) FROM w))
    ELSE u
    END FROM generate_series(9, 15) u ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 10;
TABLE with_a;
ABORT;

BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (generate_series(6, 10)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 100 RETURNING i
) INSERT INTO with_a SELECT CASE
    WHEN u < 11 THEN u
    WHEN u < 13 THEN -(u + (SELECT max(i) FROM w))
    ELSE u
    END FROM generate_series(10, 15) u ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 10;
TABLE with_a;
ABORT;
