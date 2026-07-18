CREATE TABLE yb_char_in_value (k INT PRIMARY KEY, c CHAR);

INSERT INTO yb_char_in_value VALUES (1, 'a');
INSERT INTO yb_char_in_value VALUES (2, 'b');
INSERT INTO yb_char_in_value VALUES (3, 'c');
INSERT INTO yb_char_in_value VALUES (4, 'd');
INSERT INTO yb_char_in_value VALUES (5, 'e');
INSERT INTO yb_char_in_value VALUES (10, NULL);
INSERT INTO yb_char_in_value VALUES (11, chr(1040));
INSERT INTO yb_char_in_value VALUES (12, chr(1041));
INSERT INTO yb_char_in_value VALUES (13, chr(1042));
INSERT INTO yb_char_in_value VALUES (14, chr(1043));
INSERT INTO yb_char_in_value VALUES (15, chr(1044));

SELECT * FROM yb_char_in_value ORDER BY k;
SELECT * FROM yb_char_in_value ORDER BY c;
SELECT * FROM yb_char_in_value WHERE k = 1;
SELECT * FROM yb_char_in_value WHERE c = 'a';
SELECT * FROM yb_char_in_value WHERE c = chr(1041);

DROP TABLE yb_char_in_value;

CREATE TABLE yb_char_in_key (ck CHAR PRIMARY KEY);

INSERT INTO yb_char_in_key VALUES ('a');
INSERT INTO yb_char_in_key VALUES ('b');
INSERT INTO yb_char_in_key VALUES ('c');
INSERT INTO yb_char_in_key VALUES ('d');
INSERT INTO yb_char_in_key VALUES ('e');
INSERT INTO yb_char_in_key VALUES (chr(1040));
INSERT INTO yb_char_in_key VALUES (chr(1041));
INSERT INTO yb_char_in_key VALUES (chr(1042));
INSERT INTO yb_char_in_key VALUES (chr(1043));
INSERT INTO yb_char_in_key VALUES (chr(1044));

SELECT * FROM yb_char_in_key ORDER BY ck;
SELECT * FROM yb_char_in_key WHERE ck = 'a';
SELECT * FROM yb_char_in_key WHERE ck = chr(1042);

set yb_enable_expression_pushdown to off;
SELECT * FROM yb_char_in_key ORDER BY ck;
SELECT * FROM yb_char_in_key WHERE ck = 'a';
SELECT * FROM yb_char_in_key WHERE ck = chr(1042);
set yb_enable_expression_pushdown to on;

DROP TABLE yb_char_in_key;

--
-- Now test longer arrays of char
--

CREATE TABLE yb_charn_in_value(f1 char(4));

INSERT INTO yb_charn_in_value(f1) VALUES ('a');
INSERT INTO yb_charn_in_value(f1) VALUES ('ab');
INSERT INTO yb_charn_in_value(f1) VALUES ('abc');
INSERT INTO yb_charn_in_value(f1) VALUES ('abc ');
INSERT INTO yb_charn_in_value(f1) VALUES (' abc');
INSERT INTO yb_charn_in_value(f1) VALUES ('abcd');
INSERT INTO yb_charn_in_value(f1) VALUES ('abcde');
INSERT INTO yb_charn_in_value(f1) VALUES ('abcd    ');

SELECT * FROM yb_charn_in_value ORDER BY f1;
SELECT * FROM yb_charn_in_value WHERE f1 = 'ab';
SELECT * FROM yb_charn_in_value WHERE f1 = 'abcd';

DROP TABLE yb_charn_in_value;

CREATE TABLE yb_charn_in_key(f1 char(4) PRIMARY KEY);

INSERT INTO yb_charn_in_key(f1) VALUES ('a');
INSERT INTO yb_charn_in_key(f1) VALUES ('ab');
INSERT INTO yb_charn_in_key(f1) VALUES ('abc');
INSERT INTO yb_charn_in_key(f1) VALUES ('abc ');
INSERT INTO yb_charn_in_key(f1) VALUES (' abc');
INSERT INTO yb_charn_in_key(f1) VALUES ('abcd');
INSERT INTO yb_charn_in_key(f1) VALUES ('abcde');
INSERT INTO yb_charn_in_key(f1) VALUES ('abcd    ');

SELECT * FROM yb_charn_in_key ORDER BY f1;
SELECT * FROM yb_charn_in_key WHERE f1 = 'ab';
SELECT * FROM yb_charn_in_key WHERE f1 = 'abcd';

DROP TABLE yb_charn_in_key;
