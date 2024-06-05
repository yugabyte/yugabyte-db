\set VERBOSITY terse
SET client_encoding = utf8;

SET search_path TO public, oracle;

--
-- test type modifier related rules
--

-- ERROR (typmod >= 1)
CREATE TABLE foo (a VARCHAR2(0));

-- ERROR (number of typmods = 1)
CREATE TABLE foo (a VARCHAR2(10, 1));

-- OK
CREATE TABLE foo (a VARCHAR(5000));

-- cleanup
DROP TABLE foo;

-- OK
CREATE TABLE foo (a VARCHAR2(5));

CREATE INDEX ON foo(a);

--
-- test that no value longer than maxlen is allowed
--

-- ERROR (length > 5)
INSERT INTO foo VALUES ('abcdef');

-- ERROR (length > 5);
-- VARCHAR2 does not truncate blank spaces on implicit coercion
INSERT INTO foo VALUES ('abcde  ');

-- OK
INSERT INTO foo VALUES ('abcde');

-- OK
INSERT INTO foo VALUES ('abcdef'::VARCHAR2(5));

-- OK
INSERT INTO foo VALUES ('abcde  '::VARCHAR2(5));

--OK
INSERT INTO foo VALUES ('abc'::VARCHAR2(5));

--
-- test whitespace semantics on comparison
--

-- equal
SELECT 'abcde   '::VARCHAR2(10) = 'abcde   '::VARCHAR2(10);

-- not equal
SELECT 'abcde  '::VARCHAR2(10) = 'abcde   '::VARCHAR2(10);

--
-- test string functions created for varchar2
--

-- substrb(varchar2, int, int)
SELECT substrb('ABCありがとう'::VARCHAR2, 7, 6);

-- returns 'f' (emtpy string is not NULL)
SELECT substrb('ABCありがとう'::VARCHAR2, 7, 0) IS NULL;

-- If the starting position is zero or less, then return from the start
-- of the string adjusting the length to be consistent with the "negative start"
-- per SQL.
SELECT substrb('ABCありがとう'::VARCHAR2, 0, 4);

-- substrb(varchar2, int)
SELECT substrb('ABCありがとう', 5);

-- strposb(varchar2, varchar2)
SELECT strposb('ABCありがとう', 'りが');

-- returns 1 (start of the source string)
SELECT strposb('ABCありがとう', '');

-- returns 0
SELECT strposb('ABCありがとう', 'XX');

-- returns 't'
SELECT strposb('ABCありがとう', NULL) IS NULL;

-- lengthb(varchar2)
SELECT lengthb('ABCありがとう');

-- returns 0
SELECT lengthb('');

-- returs 't'
SELECT lengthb(NULL) IS NULL;

-- null safe concat (disabled by default)
SELECT NULL || 'hello'::varchar2 || NULL;

SET orafce.varchar2_null_safe_concat TO true;
SELECT NULL || 'hello'::varchar2 || NULL;
