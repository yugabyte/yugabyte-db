-- Test for the regexp_*() function
\set ECHO none
SET client_min_messages = warning;
SET client_encoding = utf8;
\set VERBOSITY terse
\set ECHO all

SET search_path TO oracle, "$user", public, pg_catalog;

----
-- Tests for REGEXP_LIKE()
----

-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('a'||CHR(10)||'d', 'a.d'); -> NULL
SELECT REGEXP_LIKE('a'||CHR(10)||'d', 'a.d');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('a'||CHR(10)||'d', 'a.d', 'm'); -> NULL
SELECT REGEXP_LIKE('a'||CHR(10)||'d', 'a.d', 'm');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('a'||CHR(10)||'d', 'a.d', 'n'); -> 1
SELECT REGEXP_LIKE('a'||CHR(10)||'d', 'a.d', 'n');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('Steven', '^Ste(v|ph)en$'); -> 1
SELECT REGEXP_LIKE('Steven', '^Ste(v|ph)en$');
-- ORACLE> SELECT 1 FROM DUAL WHERE regexp_like('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar'); -> NULL
SELECT REGEXP_LIKE('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar');
-- ORACLE> SELECT 1 FROM DUAL WHERE regexp_like('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', 'bar'); -> 1
SELECT REGEXP_LIKE('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', 'bar');
-- ORACLE> SELECT 1 FROM DUAL WHERE regexp_like('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 'm'); -> 1
SELECT REGEXP_LIKE('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 'm');
-- ORACLE> SELECT 1 FROM DUAL WHERE regexp_like('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 'n'); -> NULL
SELECT REGEXP_LIKE('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 'n');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('GREEN', '([aeiou])\1'); -> NULL
SELECT REGEXP_LIKE('GREEN', '([aeiou])\1');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('GREEN', '([aeiou])\1', 'i'); -> 1
SELECT REGEXP_LIKE('GREEN', '([aeiou])\1', 'i');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '([aeiou])\1', 'i'); -> 1
SELECT REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '([aeiou])\1', 'i');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 'i'); -> NULL
SELECT REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 'i');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '([aeiou])\1', 'in'); -> 1
SELECT REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '([aeiou])\1', 'in');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 'in'); -> NULL
SELECT REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 'in');
-- ORACLE> SELECT 1 FROM DUAL WHERE REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 'im'); -> 1
SELECT REGEXP_LIKE('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 'im');

----
-- Tests for REGEXP_COUNT()
----

-- ORACLE> SELECT REGEXP_COUNT('a,b' || chr(10) || 'c','[^,]+') FROM DUAL; -> 2
SELECT REGEXP_COUNT('a,b' || chr(10) || 'c','[^,]+');
-- ORACLE> SELECT REGEXP_COUNT('a,b' || chr(10) || 'c','b.c') FROM DUAL; -> 0
SELECT REGEXP_COUNT('a,b' || chr(10) || 'c','b.c');
-- ORACLE> SELECT REGEXP_COUNT('a'||CHR(10)||'d', 'a.d') FROM DUAL; -> 0
SELECT REGEXP_COUNT('a'||CHR(10)||'d', 'a.d');
-- ORACLE> SELECT REGEXP_COUNT('a'||CHR(10)||'d', 'a.d', 1, 'm') FROM DUAL; -> 0
SELECT REGEXP_COUNT('a'||CHR(10)||'d', 'a.d', 1, 'm');
-- ORACLE> SELECT REGEXP_COUNT('a'||CHR(10)||'d', 'a.d', 1, 'n') FROM DUAL; -> 1
SELECT REGEXP_COUNT('a'||CHR(10)||'d', 'a.d', 1, 'n');
-- ORACLE> SELECT REGEXP_COUNT('Steven', '^Ste(v|ph)en$') FROM DUAL; -> 1
SELECT REGEXP_COUNT('Steven', '^Ste(v|ph)en$');
-- ORACLE> SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar') FROM DUAL; -> 0
SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar');
-- ORACLE> SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', 'bar') FROM DUAL; -> 1
SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', 'bar');
-- ORACLE> SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 0, 'm') FROM DUAL; -> ORA-01428: argument '0' is out of range
SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 0, 'm');
-- ORACLE> SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 1, 'm') FROM DUAL; -> 1
SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 1, 'm');
-- ORACLE> SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 1, 'n') FROM DUAL; -> 0
SELECT REGEXP_COUNT('foo' || chr(10) || 'bar' || chr(10) || 'bequq' || chr(10) || 'baz', '^bar', 1, 'n');
-- ORACLE> SELECT REGEXP_COUNT('GREEN', '([aeiou])\1') FROM DUAL; -> 0
SELECT REGEXP_COUNT('GREEN', '([aeiou])\1');
-- ORACLE> SELECT REGEXP_COUNT('GREEN', '([aeiou])\1', 1, 'i') FROM DUAL; -> 1
SELECT REGEXP_COUNT('GREEN', '([aeiou])\1', 1, 'i');
-- ORACLE> SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '([aeiou])\1', 1, 'i') FROM DUAL; -> 1
SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '([aeiou])\1', 1, 'i');
-- ORACLE> SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 1, 'i') FROM DUAL; -> 0
SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 1, 'i');
-- ORACLE> SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '([aeiou])\1', 1, 'in') FROM DUAL; -> 1
SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '([aeiou])\1', 1, 'in');
-- ORACLE> SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 1, 'in') FROM DUAL; -> 0
SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 1, 'in');
-- ORACLE> SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 1, 'im') FROM DUAL; -> 1
SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', 1, 'im');
-- ORACLE> SELECT REGEXP_COUNT('123123123123123', '(12)3', 1, 'i') REGEXP_COUNT FROM DUAL; -> 5
SELECT REGEXP_COUNT('123123123123123', '(12)3', 1, 'i');
-- ORACLE> SELECT REGEXP_COUNT('123123123123', '123', 3, 'i') COUNT FROM DUAL; -> 3
SELECT REGEXP_COUNT('123123123123', '123', 3, 'i');
-- ORACLE> SELECT REGEXP_COUNT('ABC123', '[A-Z]'), REGEXP_COUNT('A1B2C3', '[A-Z]') FROM DUAL; -> 3 | 3
SELECT REGEXP_COUNT('ABC123', '[A-Z]'), oracle.REGEXP_COUNT('A1B2C3', '[A-Z]');
-- ORACLE> SELECT REGEXP_COUNT('ABC123', '[A-Z][0-9]'), REGEXP_COUNT('A1B2C3', '[A-Z][0-9]') FROM DUAL; -> 1 | 3
SELECT REGEXP_COUNT('ABC123', '[A-Z][0-9]'), oracle.REGEXP_COUNT('A1B2C3', '[A-Z][0-9]');
-- ORACLE> SELECT REGEXP_COUNT('ABC123', '^[A-Z][0-9]'), REGEXP_COUNT('A1B2C3', '^[A-Z][0-9]') FROM DUAL; -> 0 | 1
SELECT REGEXP_COUNT('ABC123', '^[A-Z][0-9]'), oracle.REGEXP_COUNT('A1B2C3', '^[A-Z][0-9]');
-- ORACLE> SELECT REGEXP_COUNT('ABC123', '([A-Z][0-9]){2}'), REGEXP_COUNT('A1B2C3', '([A-Z][0-9]){2}') FROM DUAL; -> 0 | 1
SELECT REGEXP_COUNT('ABC123', '([A-Z][0-9]){2}'), oracle.REGEXP_COUNT('A1B2C3', '([A-Z][0-9]){2}');
-- Check negatives values that must throw an error
SELECT REGEXP_COUNT('ORANGE' || chr(10) || 'GREEN', '^..([aeiou])\1', -1, 'i');

----
-- Tests for REGEXP_INSTR()
----

-- ORACLE> SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))') FROM DUAL; -> 1
SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))');
-- ORACLE> SELECT REGEXP_INSTR('1234567890', '(4(56)(78))') FROM DUAL; -> 4
SELECT REGEXP_INSTR('1234567890', '(4(56)(78))');
-- ORACLE> SELECT regexp_instr('1234567890', '123(4(56)(78))', 3) FROM DUAL; -> 0
SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 3);
-- ORACLE> SELECT REGEXP_INSTR('1234567890', '(4(56)(78))', 3) FROM DUAL; -> 4
SELECT REGEXP_INSTR('1234567890', '(4(56)(78))', 3);
-- ORACLE> SELECT REGEXP_INSTR('500 Oracle Parkway, Redwood Shores, CA', '[^ ]+', 1, 6) FROM DUAL; -> 37
SELECT REGEXP_INSTR('500 Oracle Parkway, Redwood Shores, CA', '[^ ]+', 1, 6);
-- ORACLE> SELECT REGEXP_INSTR('500 Oracle Parkway, Redwood Shores, CA', '[S|R|P][[:alpha:]]{6}', 3, 2, 0) FROM DUAL; -> 21
SELECT REGEXP_INSTR('500 Oracle Parkway, Redwood Shores, CA', '[S|R|P][[:alpha:]]{6}', 3, 2, 0);
-- ORACLE> SELECT REGEXP_INSTR('500 Oracle Parkway, Redwood Shores, CA', '[S|R|P][[:alpha:]]{6}', 3, 2, 1) FROM DUAL; -> 28
SELECT REGEXP_INSTR('500 Oracle Parkway, Redwood Shores, CA', '[S|R|P][[:alpha:]]{6}', 3, 2, 1);
-- ORACLE> SELECT REGEXP_INSTR('500 Oracle Parkway, Redwood Shores, CA', '[s|r|p][[:alpha:]]{6}', 3, 2, 1, 'i') FROM DUAL; -> 28
SELECT REGEXP_INSTR('500 Oracle Parkway, Redwood Shores, CA', '[q|r|p][[:alpha:]]{6}', 3, 2, 1, 'i');
-- ORACLE> SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 0) FROM DUAL; -> 1
SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 0);
-- ORACLE> SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 1) FROM DUAL; -> 1
SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 1);
-- ORACLE> SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 2) FROM DUAL; -> 4
SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 2);
-- ORACLE> SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 4) FROM DUAL; -> 7
SELECT REGEXP_INSTR('1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 4);
-- ORACLE> SELECT REGEXP_INSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 4) FROM DUAL; -> 7
SELECT REGEXP_INSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 1, 0, 'i', 4);
-- ORACLE> SELECT REGEXP_INSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 2, 0, 'i', 4) FROM DUAL; -> 18
SELECT REGEXP_INSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 2, 0, 'i', 4);
-- ORACLE> SELECT REGEXP_INSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 2, 1, 'i', 4) FROM DUAL; -> 20
SELECT REGEXP_INSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 2, 1, 'i', 4);
-- ORACLE> SELECT REGEXP_INSTR('1234567890 1234567890 1234567890', '(123)(4(56)(78))', 1, 3, 0,'i', 4) FROM DUAL; -> 29
SELECT REGEXP_INSTR('1234567890 1234567890 1234567890', '(123)(4(56)(78))', 1, 3, 0, 'i', 4);
-- ORACLE> SELECT REGEXP_INSTR('1234567890 1234567890 1234567890', '(123)(4(56)(78))', 1, 3, 1,'i', 4) FROM DUAL; -> 31
SELECT REGEXP_INSTR('1234567890 1234567890 1234567890', '(123)(4(56)(78))', 1, 3, 1, 'i', 4);
-- DROP TABLE regexp_temp;
-- CREATE TABLE regexp_temp(empName varchar2(20), emailID varchar2(20));
-- INSERT INTO regexp_temp (empName, emailID) VALUES ('John Doe', 'johndoe@example.com');
-- INSERT INTO regexp_temp (empName, emailID) VALUES ('Jane Doe', 'janedoe');
-- COMMIT;
CREATE TEMPORARY TABLE regexp_temp(empName varchar(20), emailID varchar(20));
INSERT INTO regexp_temp (empName, emailID) VALUES ('John Doe', 'johndoe@example.com');
INSERT INTO regexp_temp (empName, emailID) VALUES ('Jane Doe', 'janedoe');
-- -- ORACLE> SELECT emailID, REGEXP_INSTR(emailID, '\w+@\w+(\.\w+)+') "IS_A_VALID_EMAIL" FROM regexp_temp;
-- EMAILID              IS_A_VALID_EMAIL
-- -------------------- ----------------
-- johndoe@example.com                    1
-- example.com                            0
SELECT emailID, REGEXP_INSTR(emailID, '\w+@\w+(\.\w+)+') FROM regexp_temp;
-- -- ORACLE> SELECT emailID, REGEXP_INSTR(emailID, '\w+@\w+(\.\w+)+', 1, 1, 0, 'i', 0) "IS_A_VALID_EMAIL" FROM regexp_temp;
-- EMAILID              IS_A_VALID_EMAIL
-- -------------------- ----------------
-- johndoe@example.com                    1
-- example.com                            0
SELECT emailID, REGEXP_INSTR(emailID, '\w+@\w+(\.\w+)+', 1, 1, 0, 'i', 0) FROM regexp_temp;
-- -- ORACLE> SELECT emailID, REGEXP_INSTR(emailID, '\w+@\w+(\.\w+)+', 1, 1, 0, 'i', 1) "IS_A_VALID_EMAIL" FROM regexp_temp;
-- EMAILID              IS_A_VALID_EMAIL
-- -------------------- ----------------
-- johndoe@example.com                   16
-- example.com                            0
SELECT emailID, REGEXP_INSTR(emailID, '\w+@\w+(\.\w+)+', 1, 1, 0, 'i', 1) FROM regexp_temp;
DROP TABLE regexp_temp;
-- Check negatives values that must throw an error
SELECT REGEXP_INSTR('1234567890 1234567890 1234567890', '(123)(4(56)(78))', -1, 3, 1, 'i', 4);
SELECT REGEXP_INSTR('1234567890 1234567890 1234567890', '(123)(4(56)(78))', 1, -3, 1, 'i', 4);
SELECT REGEXP_INSTR('1234567890 1234567890 1234567890', '(123)(4(56)(78))', 1, 3, -1, 'i', 4);
SELECT REGEXP_INSTR('1234567890 1234567890 1234567890', '(123)(4(56)(78))', 1, 3, 1, 'i', -4);

-- ORACLE> SELECT REGEXP_INSTR('123 123456 1234567, 1234567 1234567 12', '[^ ]+', 1, 6) FROM DUAL; -> 37
SELECT REGEXP_INSTR('123 123456 1234567, 1234567 1234567 12', '[^ ]+', 1, 6) ;
-- ORACLE> SELECT REGEXP_INSTR('123 123456 1234567, 1234567 1234567 12', '[^ ]+', 1, 6, 1) FROM DUAL; -> 39
SELECT REGEXP_INSTR('123 123456 1234567, 1234567 1234567 12', '[^ ]+', 1, 6, 1) ;
-- ORACLE> SELECT REGEXP_INSTR('123 123456 1234567, 1234567 1234567 12', '[^ ]+', 1, 6, 1, 'i') FROM DUAL; -> 39
SELECT REGEXP_INSTR('123 123456 1234567, 1234567 1234567 12', '[^ ]+', 1, 6, 1, 'i') ;

----
-- Tests for REGEXP_SUBSTR()
----

-- ORACLE> SELECT REGEXP_SUBSTR('a,b'||chr(10)||'c','[^,]+',1,2) FROM DUAL; -> b and c
SELECT REGEXP_SUBSTR('a,b'||chr(10)||'c','[^,]+',1,2);
-- ORACLE> SELECT REGEXP_SUBSTR('a,b'||chr(10)||'c','.',1,4) FROM DUAL; -> c
SELECT REGEXP_SUBSTR('a,b'||chr(10)||'c','.',1,4);
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',[^,]+') FROM DUAL; -> , zipcode town
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',[^,]+');
-- ORACLE> SELECT REGEXP_SUBSTR('http://www.example.com/products', 'http://([[:alnum:]]+\.?){3,4}/?') FROM DUAL; -> http://www.example.com/
SELECT REGEXP_SUBSTR('http://www.example.com/products', 'http://([[:alnum:]]+\.?){3,4}/?');
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',[^,]+', 24) FROM DUAL; -> , FR
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',[^,]+', 24);
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',[^,]+', 1, 1) FROM DUAL; -> , zipcode town
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',[^,]+', 1, 1);
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',[^,]+', 1, 2) FROM DUAL; -> , FR
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',[^,]+', 1, 2);
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+[Zf][^,]+', 1, 1) FROM DUAL; -> NULL
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+[Zf][^,]+', 1, 1);
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+[Zf][^,]+', 1, 1, 'i') FROM DUAL; -> , zipcode town
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+[Zf][^,]+', 1, 1, 'i');
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+[Zf][^,]+', 1, 1, 'i', 0) FROM DUAL; -> , zipcode town
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+[Zf][^,]+', 1, 1, 'i', 0);
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+[Zf][^,]+', 1, 1, 'i', 1) FROM DUAL; -> NULL
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+[Zf][^,]+', 1, 1, 'i', 1);
-- ORACLE> SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+([Zf][^,]+)', 1, 1, 'i', 1) FROM DUAL; -> zipcode town
SELECT REGEXP_SUBSTR('number of your street, zipcode town, FR', ',\s+([Zf][^,]+)', 1, 1, 'i', 1);
-- ORACLE> SELECT REGEXP_SUBSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 1, 'i', 4) FROM DUAL; -> 78
SELECT REGEXP_SUBSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 1, 'i', 4);
-- ORACLE> SELECT REGEXP_SUBSTR('1234567890 1234557890', '(123)(4(5[56])(78))', 1, 2, 'i', 3) FROM DUAL; -> 55
SELECT REGEXP_SUBSTR('1234567890 1234557890', '(123)(4(5[56])(78))', 1, 2, 'i', 3);
-- ORACLE> SELECT REGEXP_SUBSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 1, 'i', 0) FROM DUAL; -> 12345678
SELECT REGEXP_SUBSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 1, 'i', 0);
-- Check negatives values that must throw an error
SELECT REGEXP_SUBSTR('1234567890 1234567890', '(123)(4(56)(78))', -1, 1, 'i', 0);
SELECT REGEXP_SUBSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, -1, 'i', 0);
SELECT REGEXP_SUBSTR('1234567890 1234567890', '(123)(4(56)(78))', 1, 1, 'i', -1);

----
-- Tests for REGEXP_REPLACE()
----

-- ORACLE> SELECT REGEXP_REPLACE('512.123.4567', '([[:digit:]]{3})\.([[:digit:]]{3})\.([[:digit:]]{4})', '(\1) \2-\3') FROM DUAL; -> (512) 123-4567
SELECT REGEXP_REPLACE('512.123.4567', '([[:digit:]]{3})\.([[:digit:]]{3})\.([[:digit:]]{4})', '(\1) \2-\3');
-- ORACLE> SELECT REGEXP_REPLACE('512.123.4567 612.123.4567', '([[:digit:]]{3})\.([[:digit:]]{3})\.([[:digit:]]{4})', '(\1) \2-\3') FROM DUAL; -> (512) 123-4567 (612) 123-4567
SELECT oracle.REGEXP_REPLACE('512.123.4567 612.123.4567', '([[:digit:]]{3})\.([[:digit:]]{3})\.([[:digit:]]{4})', '(\1) \2-\3');
-- ORACLE> SELECT REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ') FROM DUAL; -> number your street, zipcode town, FR
SELECT oracle.REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ');
-- ORACLE> SELECT REGEXP_REPLACE('number   your     street,'||CHR(10)||'    zipcode  town, FR', '( ){2,}', ' ') FROM DUAL; -> number your street,
-- zipcode town, FR
SELECT oracle.REGEXP_REPLACE('number   your     street,'||CHR(10)||'    zipcode  town, FR', '( ){2,}', ' ');
-- ORACLE> SELECT REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9) FROM DUAL; -> number    your street, zipcode town, FR
SELECT oracle.REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9);
-- ORACLE> SELECT REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9, 0) FROM DUAL; -> number    your street, zipcode town, FR
SELECT oracle.REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9, 0);
-- ORACLE> SELECT REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9, 2) FROM DUAL; -> number    your     street, zipcode  town, FR
SELECT oracle.REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9, 2);
-- ORACLE> SELECT REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9, 2, 'm') FROM DUAL; -> number    your     street, zipcode  town, FR
SELECT oracle.REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9, 2, 'm');
-- ORACLE> SELECT REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '([EURT]){2,}', '[\1]', 9, 2, 'i') FROM DUAL; -> number    your     s[t],    zipcode  town, FR
SELECT oracle.REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '([EURT]){2,}', '[\1]', 9, 2, 'i');
-- ORACLE> SELECT REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '[ ]{2,}', ' ', 9, 2) FROM DUAL; -> number    your     street, zipcode  town, FR
SELECT oracle.REGEXP_REPLACE('number   your     street,    zipcode  town, FR', '( ){2,}', ' ', 9, 2);

-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'A|e|i|o|u', 'X', 1, 2) FROM DUAL; -> A PXstgreSQL function
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'A|e|i|o|u', 'X', 1, 2);
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 0, 'i') FROM DUAL; -> X PXstgrXSQL fXnctXXn
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 0, 'i');
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 1, 'i') FROM DUAL; -> X PostgreSQL function
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 1, 'i');
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 2, 'i') FROM DUAL; -> A PXstgreSQL function
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 2, 'i');
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 3, 'i') FROM DUAL; -> A PostgrXSQL function
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 3, 'i');
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 9, 'i') FROM DUAL; -> A PostgreSQL function
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 9, 'i');
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'A|e|i|o|u', 'X', 1, 9) FROM DUAL; -> A PostgreSQL function
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'A|e|i|o|u', 'X', 1, 9);
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', -1, 0, 'i') FROM DUAL; -> ORA-01428
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', -1, 0, 'i');
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, -1, 'i') FROM DUAL; -> ORA-01428
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, -1, 'i');
-- ORACLE> SELECT REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 1, 'g') FROM DUAL; -> ORA-01760
SELECT oracle.REGEXP_REPLACE ('A PostgreSQL function', 'a|e|i|o|u', 'X', 1, 1, 'g');

--
-- Test NULL input in the regexp_* functions that must returned NULL except for the modifier
-- or regexp flag. There is an exception with regexp_replace(), if the pattern is null (second
-- parameter) the original string is returned. We don't test functions witht the STRICT attribute
--
SELECT oracle.REGEXP_LIKE(NULL, '\d+', 'i');
SELECT oracle.REGEXP_LIKE('1234', NULL, 'i');
SELECT oracle.REGEXP_LIKE('1234', '\d+', NULL);
SELECT oracle.REGEXP_LIKE('1234', '\d+', '');
SELECT oracle.REGEXP_COUNT('1234', '\d', NULL) ;
SELECT oracle.REGEXP_COUNT('1234', '\d', 1, NULL) ;
SELECT oracle.REGEXP_COUNT('1234', '\d', 1, '') ;
SELECT oracle.REGEXP_COUNT('1234', '\d', NULL, NULL) ;
SELECT oracle.REGEXP_COUNT(NULL, '4', 1, 'i');
SELECT oracle.REGEXP_INSTR('1234', '4', NULL);
SELECT oracle.REGEXP_INSTR('1234', '4', 1, NULL);
SELECT oracle.REGEXP_INSTR('1234', '4', 1, 1, NULL);
SELECT oracle.REGEXP_INSTR('1234', '4', 1, 1, 1, NULL);
SELECT oracle.REGEXP_INSTR('1234', '4', 1, 1, 1, NULL, 0);
SELECT oracle.REGEXP_INSTR('1234', '4', 1, 1, 0, NULL);
SELECT oracle.REGEXP_INSTR('1234', '4', 1, 1, 0, 'i', NULL);
SELECT oracle.REGEXP_INSTR('1234', '4', 1, 1, 0, '', NULL);
SELECT oracle.REGEXP_INSTR(NULL, '4', 1, 1, 0, 'i', 2);
SELECT oracle.REGEXP_INSTR(NULL, '4', 1, 1, 0, 'i', 2);
SELECT oracle.REGEXP_SUBSTR('1234', '1(.*)', null);
SELECT oracle.REGEXP_SUBSTR('1234', '234', 1, null);
SELECT oracle.REGEXP_SUBSTR('1234', '234', 1, 1, null);
SELECT oracle.REGEXP_SUBSTR('1234', '234', 1, 1, '');
SELECT oracle.REGEXP_SUBSTR('1234', '234', 1, 1, 'i', null);
-- test for capture group
SELECT oracle.REGEXP_SUBSTR('1234', '2(3)(4)', 1, 1, 'i', 1);
SELECT oracle.REGEXP_SUBSTR('1234', '2(3)(4)', 1, 1, 'i', 2);
SELECT oracle.REGEXP_SUBSTR('1234', '2(3)(4)', 1, 1, 'i', 0);

-- ORACLE> SELECT REGEXP_REPLACE(null, '\d', 'a') FROM DUAL; -> NULL
SELECT oracle.REGEXP_REPLACE(null, '\d', 'a');
-- ORACLE> SELECT REGEXP_REPLACE('1234', null, 'a') FROM DUAL; -> 1234
SELECT oracle.REGEXP_REPLACE('1234', null, 'a');
-- ORACLE> SELECT REGEXP_REPLACE('1234', null, null) FROM DUAL; -> 1234
SELECT oracle.REGEXP_REPLACE('1234', null, null);
-- ORACLE> SELECT REGEXP_REPLACE('1234', '\d', null) FROM DUAL; -> NULL
SELECT oracle.REGEXP_REPLACE('1234', '\d', null);
-- ORACLE> SELECT REGEXP_REPLACE('1234', '\d', 'a', null) FROM DUAL; -> NULL
SELECT oracle.REGEXP_REPLACE('1234', '\d', 'a', null);
-- ORACLE> SELECT REGEXP_REPLACE('1234', null, 'a', 2) FROM DUAL; -> 1234
SELECT oracle.REGEXP_REPLACE('1234', null, 'a', 2);
-- ORACLE> SELECT REGEXP_REPLACE('1234', null, 'a', null) FROM DUAL; -> NULL
SELECT oracle.REGEXP_REPLACE('1234', null, 'a', null);
-- ORACLE> SELECT REGEXP_REPLACE('1234', null, 'a', 1) FROM DUAL; -> 1234
SELECT oracle.REGEXP_REPLACE('1234', null, 'a', 1);
-- ORACLE> SELECT REGEXP_REPLACE('1234', null, 'a', 1, null) FROM DUAL; -> NULL
SELECT oracle.REGEXP_REPLACE('1234', null, 'a', 1, null);
-- ORACLE> SELECT REGEXP_REPLACE('1234', '\d', 'a', 1, null) FROM DUAL; -> NULL
SELECT oracle.REGEXP_REPLACE('1234', '\d', 'a', 1, null);
-- ORACLE> SELECT REGEXP_REPLACE('1234', '\d', 'a', 1, 1, null) FROM DUAL; -> a234
SELECT oracle.REGEXP_REPLACE('1234', '\d', 'a', 1, 1, null);
-- ORACLE> SELECT REGEXP_REPLACE('1234', '\d', 'a', 1, NULL, 'i') FROM DUAL; -> NULL
SELECT oracle.REGEXP_REPLACE('1234', '\d', 'a', 1, NULL, 'i');
-- ORACLE> SELECT REGEXP_REPLACE('1234', null, 'a', 1, 1, 'i') FROM DUAL; -> 1234
SELECT oracle.REGEXP_REPLACE('1234', null, 'a', 1, 1, 'i');
