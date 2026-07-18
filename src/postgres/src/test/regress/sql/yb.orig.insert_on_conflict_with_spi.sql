--
-- Depends on yb.orig.insert_on_conflict_with test.
--

BEGIN;
-- With batch size >= 3, get cannot affect row a second time between the two 2s.
-- insert order:                        | x, x
--
--                 x | x | x | x | x, x
--                  \  /   \    \
--                   \/     \    \
--                x, x,     x, x, x
WITH w1 AS (
    INSERT INTO with_a VALUES
        (44),
        (45)
    ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i
), w2 AS (
    INSERT INTO with_a VALUES
        (1),
        (100),
        (2),
        (5),
        (25),
        (3)
    ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i
) INSERT INTO with_a VALUES
    (0),
    ((SELECT i + 3 FROM w2 LIMIT 1)), -- 2
    ((SELECT abs(min(i)) FROM (SELECT i FROM w2 LIMIT 3) l)), -- 1
    (4),
    ((SELECT abs(min(i)) FROM (SELECT i FROM w2 LIMIT 4) l)) -- 5
ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT max(i) FROM (SELECT i FROM w2 LIMIT 2) l) RETURNING i;
TABLE with_a;
ABORT;

-- Same thing with yb_run_spi.
BEGIN;
-- insert order:                       | x, x
--
--                 x | x | x | x, x, x
--                  \  /   \
--                   \/     \
--                x, x,     x
SELECT yb_run_spi($$
WITH w1 AS (
    INSERT INTO with_a VALUES
        (44),
        (45)
    ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i
), w2 AS (
    INSERT INTO with_a VALUES
        (1),
        (100),
        (2),
        (5),
        (25),
        (3)
    ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i
) INSERT INTO with_a VALUES
    (0),
    ((SELECT i + 3 FROM w2 LIMIT 1)), -- 2
    ((SELECT abs(min(i)) FROM (SELECT i FROM w2 LIMIT 3) l)), -- 1
    (4), -- unreachable
    ((SELECT abs(min(i)) FROM (SELECT i FROM w2 LIMIT 4) l)) -- unreachable
ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT max(i) FROM (SELECT i FROM w2 LIMIT 2) l) RETURNING i;
$$, 3);
TABLE with_a;
ABORT;

-- More yb_run_spi.
BEGIN;
-- insert order: x, x, x
--                       | x, x, x
--                                    x | x | x | x, x, x
--                                     \  /   \
--                                      \/     \
--                                 | x, x,     x
SELECT yb_run_spi($$
WITH w1 AS (
    SELECT yb_run_spi($q1$
    INSERT INTO with_a VALUES
        (44), -- unreachable
        (45) -- unreachable
    ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i
    $q1$, 1)
), w2 AS (
    INSERT INTO with_a VALUES
        (1),
        (100),
        (2),
        (5),
        (25),
        ((SELECT yb_run_spi($q2$
              INSERT INTO with_a VALUES (50), (51), (52)
              ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i;
          $q2$, 5))) -- 3
    ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i
) INSERT INTO with_a VALUES
    (0),
    ((SELECT i + 3 FROM w2 LIMIT 1)), -- 2
    ((SELECT abs(min(i)) FROM (SELECT i FROM w2 LIMIT 3) l)), -- 1
    (4), -- unreachable
    ((SELECT abs(min(i)) FROM (SELECT i FROM w2 LIMIT 4) l)), -- unreachable
    ((SELECT yb_run_spi($q3$
          INSERT INTO with_a VALUES (50), (51), (52)
          ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i;
      $q3$, 5))) -- unreachable
ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT max(i) FROM (SELECT i FROM w2 LIMIT 2) l) RETURNING i;
$$, 3);
TABLE with_a;
ABORT;

-- Same thing without outer yb_run_spi.
BEGIN;
-- insert order: x, x, x
--                       | x, x, x
--                                    x | x | x | x | x, x
--                                     \  /   \    \
--                                      \/     \    \
--                                 | x, x,     x, x, x, x
WITH w1 AS (
    SELECT yb_run_spi($q1$
    INSERT INTO with_a VALUES
        (44), -- unreachable
        (45) -- unreachable
    ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i
    $q1$, 1)
), w2 AS (
    INSERT INTO with_a VALUES
        (1),
        (100),
        (2),
        (5),
        (25),
        ((SELECT yb_run_spi($q2$
              INSERT INTO with_a VALUES (50), (51), (52)
              ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i;
          $q2$, 5))) -- 3
    ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i
) INSERT INTO with_a VALUES
    (0),
    ((SELECT i + 3 FROM w2 LIMIT 1)), -- 2
    ((SELECT abs(min(i)) FROM (SELECT i FROM w2 LIMIT 3) l)), -- 1
    (4),
    ((SELECT abs(min(i)) FROM (SELECT i FROM w2 LIMIT 4) l)), -- 5
    ((SELECT yb_run_spi($q3$
          INSERT INTO with_a VALUES (50), (51), (52)
          ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING i;
      $q3$, 5))) -- 3
ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT max(i) FROM (SELECT i FROM w2 LIMIT 2) l) RETURNING i;
TABLE with_a;
ABORT;
