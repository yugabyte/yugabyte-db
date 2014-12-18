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


