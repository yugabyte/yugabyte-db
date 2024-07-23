-- verify ALTER TYPE ... RENAME TO works correctly
CREATE TYPE bogus AS ENUM('good');

ALTER TYPE bogus RENAME TO bogon;

SELECT 'good'::bogus; -- fail
SELECT 'good'::bogon;

DROP TYPE bogon;

-- test ALTER table column's TYPE ... RENAME TO works correctly
CREATE TYPE happiness AS ENUM ('happy', 'very happy', 'ecstatic');
CREATE TABLE holidays (
    num_weeks integer,
    happiness_level happiness
);
INSERT INTO holidays(num_weeks,happiness_level) VALUES (4, 'happy');
INSERT INTO holidays(num_weeks,happiness_level) VALUES (6, 'very happy');
SELECT * FROM holidays ORDER BY num_weeks;

ALTER TYPE happiness RENAME TO new_name;
\dT+ new_name
\d holidays

INSERT INTO holidays(num_weeks,happiness_level) VALUES (8, 'ecstatic');
SELECT * FROM holidays ORDER BY num_weeks;

DROP TYPE happiness; -- fail
DROP TYPE new_name;  -- fail
DROP TYPE new_name CASCADE;

\d holidays
SELECT * FROM holidays ORDER BY num_weeks;

DROP TABLE holidays;

-- test ALTER table PRIMARY KEY column's TYPE ... RENAME TO works correctly
CREATE TYPE happiness AS ENUM ('happy', 'very happy');
CREATE TABLE holidays (
    num_weeks integer,
    happiness_level happiness PRIMARY KEY
);
\d holidays

INSERT INTO holidays(num_weeks,happiness_level) VALUES (4, 'happy');
INSERT INTO holidays(num_weeks,happiness_level) VALUES (6, 'very happy');
SELECT * FROM holidays ORDER BY num_weeks;

ALTER TYPE happiness RENAME TO new_name;
\dT+ new_name

ALTER TYPE new_name ADD VALUE 'ecstatic';
\dT+ new_name

INSERT INTO holidays(num_weeks,happiness_level) VALUES (8, 'ecstatic');
SELECT * FROM holidays ORDER BY num_weeks;

DROP TYPE new_name;  -- fail
DROP TYPE new_name CASCADE; -- requires table rewrite (happiness_level is a key column)
ALTER TABLE holidays DROP CONSTRAINT holidays_pkey; -- fail

\d holidays
SELECT * FROM holidays ORDER BY num_weeks;

DROP TABLE holidays;
