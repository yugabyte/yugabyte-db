SET bytea_output TO hex;
SELECT E'\\xDeAdBeEf'::bytea;
SELECT E'\\x De Ad Be Ef '::bytea;
SELECT E'\\xDeAdBeE'::bytea;
SELECT E'\\xDeAdBeEx'::bytea;
SELECT E'\\xDe00BeEf'::bytea;
SELECT E'DeAdBeEf'::bytea;
SELECT E'De\\000dBeEf'::bytea;
SELECT E'De\123dBeEf'::bytea;
SELECT E'De\\123dBeEf'::bytea;
SELECT E'De\\678dBeEf'::bytea;

SET bytea_output TO escape;
SELECT E'\\xDeAdBeEf'::bytea;
SELECT E'\\x De Ad Be Ef '::bytea;
SELECT E'\\xDe00BeEf'::bytea;
SELECT E'DeAdBeEf'::bytea;
SELECT E'De\\000dBeEf'::bytea;
SELECT E'De\\123dBeEf'::bytea;

SET bytea_output TO hex;

-- bytea in value

CREATE TABLE binary_valued (k INT PRIMARY KEY, b bytea);

INSERT INTO binary_valued (k, b) VALUES (0, decode('', 'hex'));
INSERT INTO binary_valued (k, b) VALUES (1, decode('00', 'hex'));
INSERT INTO binary_valued (k, b) VALUES (2, decode('001122', 'hex'));
INSERT INTO binary_valued (k, b) VALUES (3, decode('11220033', 'hex'));
INSERT INTO binary_valued (k, b) VALUES (4, decode('deadbeef00deadbeef', 'hex'));

SELECT * FROM binary_valued ORDER BY k;

DROP TABLE binary_valued;

-- bytea in key

CREATE TABLE binary_keyed(k bytea PRIMARY KEY, v INT);

INSERT INTO binary_keyed (k, v) VALUES (decode('', 'hex'), 0);
INSERT INTO binary_keyed (k, v) VALUES (decode('00', 'hex'), 1);
INSERT INTO binary_keyed (k, v) VALUES (decode('001122', 'hex'), 2);
INSERT INTO binary_keyed (k, v) VALUES (decode('11220033', 'hex'), 3);
INSERT INTO binary_keyed (k, v) VALUES (decode('deadbeef00deadbeef', 'hex'), 4);
INSERT INTO binary_keyed (k, v) VALUES (decode('0000', 'hex'), 5);
INSERT INTO binary_keyed (k, v) VALUES (decode('01', 'hex'), 6);

SELECT * FROM binary_keyed ORDER BY k;

DROP TABLE binary_keyed;
