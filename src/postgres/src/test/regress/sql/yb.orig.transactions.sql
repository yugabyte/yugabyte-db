
CREATE TABLE test (a int);

-- Write operation in READ ONLY transaction
BEGIN;
SET TRANSACTION READ ONLY;
INSERT INTO test VALUES(1); -- fail
END;

-- Write operation in aborted READ WRITE transaction
BEGIN;
SET TRANSACTION READ WRITE;
INSERT INTO test VALUES(1);
SELECT COUNT(*) FROM test; -- equals 1
ABORT;

SELECT COUNT(*) FROM test; -- equals 0

-- Write operation in committed READ WRITE transaction
BEGIN;
SET TRANSACTION READ WRITE;
INSERT INTO test VALUES(1);
SELECT COUNT(*) FROM test; -- equals 1
END;

SELECT COUNT(*) FROM test; -- equals 1

-- Check alternative syntax
START TRANSACTION ISOLATION LEVEL SERIALIZABLE READ WRITE;
INSERT INTO test VALUES(1);
SELECT COUNT(*) FROM test; -- equals 2
ABORT;

SELECT COUNT(*) FROM test; -- equals 1
