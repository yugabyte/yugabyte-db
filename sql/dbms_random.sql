-- Tests for package DBMS_RANDOM
SELECT dbms_random.initialize(8);
SELECT dbms_random.normal()::numeric(10, 8);
SELECT dbms_random.normal()::numeric(10, 8);
SELECT dbms_random.seed(8);
SELECT dbms_random.random();
SELECT dbms_random.seed('test');
SELECT dbms_random.string('U',5);
SELECT dbms_random.string('P',2);
SELECT dbms_random.string('x',4);
SELECT dbms_random.string('a',2);
SELECT dbms_random.string('l',3);
SELECT dbms_random.seed(5);
SELECT dbms_random.value()::numeric(10, 8);
SELECT dbms_random.value(10,15)::numeric(10, 8);
SELECT dbms_random.terminate();
