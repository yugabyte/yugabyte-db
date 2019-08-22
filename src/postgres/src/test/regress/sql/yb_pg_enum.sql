--
-- Enum tests
--

CREATE TYPE rainbow AS ENUM ('red', 'orange', 'yellow', 'green', 'blue', 'purple');

--
-- Did it create the right number of rows?
--
SELECT COUNT(*) FROM pg_enum WHERE enumtypid = 'rainbow'::regtype;

--
-- I/O functions
--
SELECT 'red'::rainbow;
SELECT 'mauve'::rainbow;

--
-- adding new values
--
CREATE TYPE planets AS ENUM ( 'venus', 'earth', 'mars' );
SELECT enumlabel, enumsortorder
FROM pg_enum
WHERE enumtypid = 'planets'::regtype
ORDER BY 2;

--
-- Basic table creation, row selection
--
CREATE TABLE enumtest (col rainbow);
INSERT INTO enumtest values ('red'), ('orange'), ('yellow'), ('green');
COPY enumtest FROM stdin;
blue
purple
\.
-- ORDER BY is needed for Yugabyte
SELECT * FROM enumtest ORDER BY col;

--
-- Operators, no index
--
SELECT * FROM enumtest WHERE col = 'orange';
SELECT * FROM enumtest WHERE col <> 'orange' ORDER BY col;
SELECT * FROM enumtest WHERE col > 'yellow' ORDER BY col;
SELECT * FROM enumtest WHERE col >= 'yellow' ORDER BY col;
SELECT * FROM enumtest WHERE col < 'green' ORDER BY col;
SELECT * FROM enumtest WHERE col <= 'green' ORDER BY col;

--
-- Cast to/from text
--
SELECT 'red'::rainbow::text || 'hithere';
SELECT 'red'::text::rainbow = 'red'::rainbow;

--
-- Aggregates
--
SELECT min(col) FROM enumtest;
SELECT max(col) FROM enumtest;
SELECT max(col) FROM enumtest WHERE col < 'green';

--
-- Index tests, force use of index
--
SET enable_seqscan = off;
SET enable_bitmapscan = off;

--
-- LSM index / opclass with the various operators
--
CREATE UNIQUE INDEX enumtest_lsm ON enumtest USING lsm (col);
SELECT * FROM enumtest WHERE col = 'orange';
SELECT * FROM enumtest WHERE col <> 'orange' ORDER BY col;
SELECT * FROM enumtest WHERE col > 'yellow' ORDER BY col;
SELECT * FROM enumtest WHERE col >= 'yellow' ORDER BY col;
SELECT * FROM enumtest WHERE col < 'green' ORDER BY col;
SELECT * FROM enumtest WHERE col <= 'green' ORDER BY col;
SELECT min(col) FROM enumtest;
SELECT max(col) FROM enumtest;
SELECT max(col) FROM enumtest WHERE col < 'green';
DROP INDEX enumtest_lsm;

--
-- Hash index / opclass with the = operator
--
CREATE INDEX enumtest_hash ON enumtest USING hash (col);
SELECT * FROM enumtest WHERE col = 'orange';
DROP INDEX enumtest_hash;

--
-- End index tests
--
RESET enable_seqscan;
RESET enable_bitmapscan;

--
-- Domains over enums
--
CREATE DOMAIN rgb AS rainbow CHECK (VALUE IN ('red', 'green', 'blue'));
SELECT 'red'::rgb;
SELECT 'purple'::rgb;
SELECT 'purple'::rainbow::rgb;
DROP DOMAIN rgb;

--
-- Arrays
--
SELECT '{red,green,blue}'::rainbow[];
SELECT ('{red,green,blue}'::rainbow[])[2];
SELECT 'red' = ANY ('{red,green,blue}'::rainbow[]);
SELECT 'yellow' = ANY ('{red,green,blue}'::rainbow[]);
SELECT 'red' = ALL ('{red,green,blue}'::rainbow[]);
SELECT 'red' = ALL ('{red,red}'::rainbow[]);

--
-- Support functions
--
SELECT enum_first(NULL::rainbow);
SELECT enum_last('green'::rainbow);
SELECT enum_range(NULL::rainbow);
SELECT enum_range('orange'::rainbow, 'green'::rainbow);
SELECT enum_range(NULL, 'green'::rainbow);
SELECT enum_range('orange'::rainbow, NULL);
SELECT enum_range(NULL::rainbow, NULL);

--
-- User functions, can't test perl/python etc here since may not be compiled.
--
CREATE FUNCTION echo_me(anyenum) RETURNS text AS $$
BEGIN
RETURN $1::text || 'omg';
END
$$ LANGUAGE plpgsql;
SELECT echo_me('red'::rainbow);
--
-- Concrete function should override generic one
--
CREATE FUNCTION echo_me(rainbow) RETURNS text AS $$
BEGIN
RETURN $1::text || 'wtf';
END
$$ LANGUAGE plpgsql;
SELECT echo_me('red'::rainbow);
--
-- If we drop the original generic one, we don't have to qualify the type
-- anymore, since there's only one match
--
DROP FUNCTION echo_me(anyenum);
SELECT echo_me('red');
DROP FUNCTION echo_me(rainbow);

--
-- Cleanup
--
DROP TABLE enumtest;
DROP TYPE rainbow;

--
-- Verify properly cleaned up
--
SELECT COUNT(*) FROM pg_type WHERE typname = 'rainbow';
SELECT * FROM pg_enum WHERE NOT EXISTS
  (SELECT 1 FROM pg_type WHERE pg_type.oid = enumtypid);
