--
-- insert...on conflict on constraint
--
CREATE TABLE tab (i int PRIMARY KEY, j int CONSTRAINT tab_j_constraint UNIQUE);
CREATE UNIQUE INDEX ON tab (j);
CREATE UNIQUE INDEX tab_idx ON tab (j);
-- Insert some data
INSERT INTO tab VALUES (1, 1);
INSERT INTO tab VALUES (200, 200);
-- Use generated constraint name for primary key.
INSERT INTO tab VALUES (1, 7) ON CONFLICT ON CONSTRAINT tab_pkey DO UPDATE
    SET j = tab.j + excluded.j;
-- Use select to verify result.
SELECT * FROM tab;
-- Use system generated name for unique index.
INSERT INTO tab VALUES (1, 200) ON CONFLICT ON CONSTRAINT tab_j_idx DO UPDATE
    SET j = tab.j + excluded.j;
-- Error: Name of index is not a constraint name.
INSERT INTO tab VALUES (1, 1) ON CONFLICT ON CONSTRAINT tab_idx DO NOTHING;
-- Use conflict for unique column
INSERT INTO tab VALUES (1, 200) ON CONFLICT (j) DO UPDATE
    SET j = tab.j + excluded.j;
-- Use SELECT to verify result.
SELECT * FROM tab;
-- Use conflict for unique constraint - noop
INSERT INTO tab VALUES (1, 400) ON CONFLICT ON CONSTRAINT tab_j_constraint DO NOTHING;
-- Use SELECT to verify result.
SELECT * FROM tab;
-- Use conflict for unique constraint - update
INSERT INTO tab VALUES (1, 400) ON CONFLICT ON CONSTRAINT tab_j_constraint DO UPDATE
    SET j = tab.j + excluded.j;
-- Use SELECT to verify result.
SELECT * FROM tab;
