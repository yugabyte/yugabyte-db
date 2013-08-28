\set ECHO none
SET client_min_messages = warning;
SET DATESTYLE TO ISO;
SET client_encoding = utf8;
\set ECHO all

--
-- test built-in date type oracle compatibility functions
--

SELECT add_months ('2003-08-01', 3);
SELECT add_months ('2003-08-01', -3);
SELECT add_months ('2003-08-21', -3);
SELECT add_months ('2003-01-31', 1);
SELECT add_months ('2008-02-28', 1);
SELECT add_months ('2008-02-29', 1);
SELECT add_months ('2008-01-31', 12);
SELECT add_months ('2008-01-31', -12);
SELECT add_months ('2008-01-31', 95903);
SELECT add_months ('2008-01-31', -80640);
SELECT add_months ('03-21-2008',3);
SELECT add_months ('21-MAR-2008',3);
SELECT add_months ('21-MAR-08',3);
SELECT add_months ('2008-MAR-21',3);
SELECT add_months ('March 21,2008',3);
SELECT add_months('03/21/2008',3);
SELECT add_months('20080321',3);
SELECT add_months('080321',3);

SELECT last_day(to_date('2003/03/15', 'yyyy/mm/dd'));
SELECT last_day(to_date('2003/02/03', 'yyyy/mm/dd'));
SELECT last_day(to_date('2004/02/03', 'yyyy/mm/dd'));
SELECT last_day('1900-02-01');
SELECT last_day('2000-02-01');
SELECT last_day('2007-02-01');
SELECT last_day('2008-02-01');

SELECT next_day ('2003-08-01', 'TUESDAY');
SELECT next_day ('2003-08-06', 'WEDNESDAY');
SELECT next_day ('2003-08-06', 'SUNDAY');
SELECT next_day ('2008-01-01', 'sun');
SELECT next_day ('2008-01-01', 'sunAAA');
SELECT next_day ('2008-01-01', 1);
SELECT next_day ('2008-01-01', 7);

SELECT months_between (to_date ('2003/01/01', 'yyyy/mm/dd'), to_date ('2003/03/14', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/07/01', 'yyyy/mm/dd'), to_date ('2003/03/14', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/07/02', 'yyyy/mm/dd'), to_date ('2003/07/02', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/08/02', 'yyyy/mm/dd'), to_date ('2003/06/02', 'yyyy/mm/dd'));
SELECT months_between ('2007-02-28', '2007-04-30');
SELECT months_between ('2008-01-31', '2008-02-29');
SELECT months_between ('2008-02-29', '2008-03-31');
SELECT months_between ('2008-02-29', '2008-04-30');
SELECT trunc(months_between('21-feb-2008', '2008-02-29'));

select length('jmenuji se Pavel Stehule'),dbms_pipe.pack_message('jmenuji se Pavel Stehule');
select length('a bydlim ve Skalici'),dbms_pipe.pack_message('a bydlim ve Skalici');
select dbms_pipe.send_message('pavel',0,1);
select dbms_pipe.send_message('pavel',0,2);
select dbms_pipe.receive_message('pavel',0);
select '>>>>'||dbms_pipe.unpack_message_text()||'<<<<';
select '>>>>'||dbms_pipe.unpack_message_text()||'<<<<';
select dbms_pipe.receive_message('pavel',0);

select dbms_pipe.purge('bob');
select dbms_pipe.reset_buffer();

select dbms_pipe.pack_message('012345678901234+1');
select dbms_pipe.send_message('bob',0,10);
select dbms_pipe.pack_message('012345678901234+2');
select dbms_pipe.send_message('bob',0,10);
select dbms_pipe.pack_message('012345678901234+3');
select dbms_pipe.send_message('bob',0,10);
--------------------------------------------
select dbms_pipe.receive_message('bob',0);
select dbms_pipe.unpack_message_text();
select dbms_pipe.receive_message('bob',0);
select dbms_pipe.unpack_message_text();
select dbms_pipe.receive_message('bob',0);
select dbms_pipe.unpack_message_text();

select dbms_pipe.unique_session_name() LIKE 'PG$PIPE$%';
select dbms_pipe.pack_message('012345678901234-1');
select dbms_pipe.send_message('bob',0,10);
select dbms_pipe.receive_message('bob',0);
select dbms_pipe.unpack_message_text();
select dbms_pipe.pack_message('012345678901234-2');
select dbms_pipe.send_message('bob',0,10);
select dbms_pipe.send_message('bob',0,10);
select dbms_pipe.receive_message('bob',0);
select dbms_pipe.unpack_message_text();

select dbms_pipe.pack_message(TO_DATE('2006-10-11', 'YYYY-MM-DD'));
select dbms_pipe.send_message('test_date');
select dbms_pipe.receive_message('test_date');
select dbms_pipe.next_item_type();
select dbms_pipe.unpack_message_date();

select dbms_pipe.pack_message(to_timestamp('2008-10-30 01:23:45', 'YYYY-MM-DD HH24:MI:SS'));
select dbms_pipe.send_message('test_timestamp');
select dbms_pipe.receive_message('test_timestamp');
select dbms_pipe.next_item_type();
select to_char(dbms_pipe.unpack_message_timestamp(), 'YYYY-MM-DD HH24:MI:SS');

select dbms_pipe.pack_message(6262626262::numeric);
select dbms_pipe.send_message('test_int');
select dbms_pipe.receive_message('test_int');
select dbms_pipe.next_item_type();
select dbms_pipe.unpack_message_number();
select dbms_pipe.purge('bob');

select name, items, "limit", private, owner from dbms_pipe.db_pipes where name = 'bob';

select PLVstr.betwn('Harry and Sally are very happy', 7, 9);
select PLVstr.betwn('Harry and Sally are very happy', 7, 9, FALSE);
select PLVstr.betwn('Harry and Sally are very happy', -3, -1);
select PLVstr.betwn('Harry and Sally are very happy', 'a', 'ry');
select PLVstr.betwn('Harry and Sally are very happy', 'a', 'ry', 1,1,FALSE,FALSE);
select PLVstr.betwn('Harry and Sally are very happy', 'a', 'ry', 2,1,TRUE,FALSE);
select PLVstr.betwn('Harry and Sally are very happy', 'a', 'y', 2,1);
select PLVstr.betwn('Harry and Sally are very happy', 'a', 'a', 2, 2);
select PLVstr.betwn('Harry and Sally are very happy', 'a', 'a', 2, 3, FALSE,FALSE);

select plvsubst.string('My name is %s %s.', ARRAY['Pavel','Stěhule']);
select plvsubst.string('My name is % %.', ARRAY['Pavel','Stěhule'], '%');
select plvsubst.string('My name is %s.', ARRAY['Stěhule']);
select plvsubst.string('My name is %s %s.', 'Pavel,Stěhule');
select plvsubst.string('My name is %s %s.', 'Pavel|Stěhule','|');
select plvsubst.string('My name is %s.', 'Stěhule');
select plvsubst.string('My name is %s.', '');
select plvsubst.string('My name is empty.', '');

select round(to_date ('22-AUG-03', 'DD-MON-YY'),'YEAR')  =  to_date ('01-JAN-04', 'DD-MON-YY');
select round(to_date ('22-AUG-03', 'DD-MON-YY'),'Q')  =  to_date ('01-OCT-03', 'DD-MON-YY');
select round(to_date ('22-AUG-03', 'DD-MON-YY'),'MONTH') =  to_date ('01-SEP-03', 'DD-MON-YY');
select round(to_date ('22-AUG-03', 'DD-MON-YY'),'DDD')  =  to_date ('22-AUG-03', 'DD-MON-YY');
select round(to_date ('22-AUG-03', 'DD-MON-YY'),'DAY')  =  to_date ('24-AUG-03', 'DD-MON-YY');
select trunc(to_date('22-AUG-03', 'DD-MON-YY'), 'YEAR')  =  to_date ('01-JAN-03', 'DD-MON-YY');
select trunc(to_date('22-AUG-03', 'DD-MON-YY'), 'Q')  =  to_date ('01-JUL-03', 'DD-MON-YY');
select trunc(to_date('22-AUG-03', 'DD-MON-YY'), 'MONTH') =  to_date ('01-AUG-03', 'DD-MON-YY');                                                                       
select trunc(to_date('22-AUG-03', 'DD-MON-YY'), 'DDD')  =  to_date ('22-AUG-03', 'DD-MON-YY');
select trunc(to_date('22-AUG-03', 'DD-MON-YY'), 'DAY')  =  to_date ('17-AUG-03', 'DD-MON-YY');

select trunc(TIMESTAMP WITH TIME ZONE '2004-10-19 10:23:54+02','YEAR') = '2004-01-01 00:00:00-08';
select trunc(TIMESTAMP WITH TIME ZONE '2004-10-19 10:23:54+02','Q') = '2004-10-01 00:00:00-07';
select trunc(TIMESTAMP WITH TIME ZONE '2004-10-19 10:23:54+02','MONTH') = '2004-10-01 00:00:00-07';
select trunc(TIMESTAMP WITH TIME ZONE '2004-10-19 10:23:54+02','DDD') = '2004-10-19 00:00:00-07';
select trunc(TIMESTAMP WITH TIME ZONE '2004-10-19 10:23:54+02','DAY') = '2004-10-17 00:00:00-07';
select trunc(TIMESTAMP WITH TIME ZONE '2004-10-19 10:23:54+02','HH') = '2004-10-19 01:00:00-07';
select trunc(TIMESTAMP WITH TIME ZONE '2004-10-19 10:23:54+02','MI') = '2004-10-19 01:23:00-07';

select next_day(to_date('01-Aug-03', 'DD-MON-YY'), 'TUESDAY')  =  to_date ('05-Aug-03', 'DD-MON-YY');                                                                 
select next_day(to_date('06-Aug-03', 'DD-MON-YY'), 'WEDNESDAY') =  to_date ('13-Aug-03', 'DD-MON-YY');
select next_day(to_date('06-Aug-03', 'DD-MON-YY'), 'SUNDAY')  =  to_date ('10-Aug-03', 'DD-MON-YY');
select instr('Tech on the net', 'e') =2;
select instr('Tech on the net', 'e', 1, 1) = 2;
select instr('Tech on the net', 'e', 1, 2) = 11;
select instr('Tech on the net', 'e', 1, 3) = 14;
select instr('Tech on the net', 'e', -3, 2) = 2;
select instr('abc', NULL) IS NULL;
select 1 = instr('abc', '');
select 1 = instr('abc', 'a');
select 3 = instr('abc', 'c');
select 0 = instr('abc', 'z');
select 1 = instr('abcabcabc', 'abca', 1);
select 4 = instr('abcabcabc', 'abca', 2);
select 0 = instr('abcabcabc', 'abca', 7);
select 0 = instr('abcabcabc', 'abca', 9);
select 4 = instr('abcabcabc', 'abca', -1);
select 1 = instr('abcabcabc', 'abca', -8);
select 1 = instr('abcabcabc', 'abca', -9);
select 0 = instr('abcabcabc', 'abca', -10);
select 1 = instr('abcabcabc', 'abca', 1, 1);
select 4 = instr('abcabcabc', 'abca', 1, 2);
select 0 = instr('abcabcabc', 'abca', 1, 3);
select oracle.substr('This is a test', 6, 2) = 'is';
select oracle.substr('This is a test', 6) =  'is a test';
select oracle.substr('TechOnTheNet', 1, 4) =  'Tech';
select oracle.substr('TechOnTheNet', -3, 3) =  'Net';
select oracle.substr('TechOnTheNet', -6, 3) =  'The';
select oracle.substr('TechOnTheNet', -8, 2) =  'On';
select oracle.substr('TechOnTheNet', -8, 0) =  '';
select oracle.substr('TechOnTheNet', -8, -1) =  '';
select concat('Tech on', ' the Net') =  'Tech on the Net';
select concat('a', 'b') =  'ab';
select concat('a', NULL) = 'a';
select concat(NULL, 'b') = 'b';
select concat('a', 2) = 'a2';
select concat(1, 'b') = '1b';
select concat(1, 2) = '12';
select concat(1, NULL) = '1';
select concat(NULL, 2) = '2';
select nvl('A'::text, 'B');
select nvl(NULL::text, 'B');
select nvl(NULL::text, NULL);
select nvl(1, 2);
select nvl(NULL, 2);
select nvl2('A'::text, 'B', 'C');
select nvl2(NULL::text, 'B', 'C');
select nvl2('A'::text, NULL, 'C');
select nvl2(NULL::text, 'B', NULL);
select nvl2(1, 2, 3);
select nvl2(NULL, 2, 3);
select lnnvl(true);
select lnnvl(false);
select lnnvl(NULL);
select decode(1, 1, 100, 2, 200);
select decode(2, 1, 100, 2, 200);
select decode(3, 1, 100, 2, 200);
select decode(3, 1, 100, 2, 200, 300);
select decode(NULL, 1, 100, NULL, 200, 300);
select decode('1'::text, '1', 100, '2', 200);
select decode(2, 1, 'ABC', 2, 'DEF');
select decode('2009-02-05'::date, '2009-02-05', 'ok');
select decode('2009-02-05 01:02:03'::timestamp, '2009-02-05 01:02:03', 'ok');

-- For type 'bpchar'
select decode('a'::bpchar, 'a'::bpchar,'postgres'::bpchar);
select decode('c'::bpchar, 'a'::bpchar,'postgres'::bpchar);
select decode('a'::bpchar, 'a'::bpchar,'postgres'::bpchar,'default value'::bpchar);
select decode('c', 'a'::bpchar,'postgres'::bpchar,'default value'::bpchar);

select decode('a'::bpchar, 'a'::bpchar,'postgres'::bpchar,'b'::bpchar,'database'::bpchar);
select decode('d'::bpchar, 'a'::bpchar,'postgres'::bpchar,'b'::bpchar,'database'::bpchar);
select decode('a'::bpchar, 'a'::bpchar,'postgres'::bpchar,'b'::bpchar,'database'::bpchar,'default value'::bpchar);
select decode('d'::bpchar, 'a'::bpchar,'postgres'::bpchar,'b'::bpchar,'database'::bpchar,'default value'::bpchar);

select decode('a'::bpchar, 'a'::bpchar,'postgres'::bpchar,'b'::bpchar,'database'::bpchar, 'c'::bpchar, 'system'::bpchar);
select decode('d'::bpchar, 'a'::bpchar,'postgres'::bpchar,'b'::bpchar,'database'::bpchar, 'c'::bpchar, 'system'::bpchar);
select decode('a'::bpchar, 'a'::bpchar,'postgres'::bpchar,'b'::bpchar,'database'::bpchar, 'c'::bpchar, 'system'::bpchar,'default value'::bpchar);
select decode('d'::bpchar, 'a'::bpchar,'postgres'::bpchar,'b'::bpchar,'database'::bpchar, 'c'::bpchar, 'system'::bpchar,'default value'::bpchar);

select decode(NULL, 'a'::bpchar, 'postgres'::bpchar, NULL,'database'::bpchar);
select decode(NULL, 'a'::bpchar, 'postgres'::bpchar, 'b'::bpchar,'database'::bpchar);
select decode(NULL, 'a'::bpchar, 'postgres'::bpchar, NULL,'database'::bpchar,'default value'::bpchar);
select decode(NULL, 'a'::bpchar, 'postgres'::bpchar, 'b'::bpchar,'database'::bpchar,'default value'::bpchar);

-- For type 'bigint'
select decode(2147483651::bigint, 2147483650::bigint,2147483650::bigint);
select decode(2147483653::bigint, 2147483651::bigint,2147483650::bigint);
select decode(2147483653::bigint, 2147483651::bigint,2147483650::bigint,9999999999::bigint);
select decode(2147483653::bigint, 2147483651::bigint,2147483650::bigint,9999999999::bigint);

select decode(2147483651::bigint, 2147483651::bigint,2147483650::bigint,2147483652::bigint,2147483651::bigint);
select decode(2147483654::bigint, 2147483651::bigint,2147483650::bigint,2147483652::bigint,2147483651::bigint);
select decode(2147483651::bigint, 2147483651::bigint,2147483650::bigint,2147483652::bigint,2147483651::bigint,9999999999::bigint);
select decode(2147483654::bigint, 2147483651::bigint,2147483650::bigint,2147483652::bigint,2147483651::bigint,9999999999::bigint);

select decode(2147483651::bigint, 2147483651::bigint,2147483650::bigint, 2147483652::bigint,2147483651::bigint, 2147483653::bigint, 2147483652::bigint);
select decode(2147483654::bigint, 2147483651::bigint,2147483650::bigint, 2147483652::bigint,2147483651::bigint, 2147483653::bigint, 2147483652::bigint);
select decode(2147483651::bigint, 2147483651::bigint,2147483650::bigint, 2147483652::bigint,2147483651::bigint, 2147483653::bigint, 2147483652::bigint,9999999999::bigint);
select decode(2147483654::bigint, 2147483651::bigint,2147483650::bigint, 2147483652::bigint,2147483651::bigint, 2147483653::bigint, 2147483652::bigint,9999999999::bigint);

select decode(NULL, 2147483651::bigint, 2147483650::bigint, NULL,2147483651::bigint);
select decode(NULL, 2147483651::bigint, 2147483650::bigint, 2147483652::bigint,2147483651::bigint);
select decode(NULL, 2147483651::bigint, 2147483650::bigint, NULL,2147483651::bigint,9999999999::bigint);
select decode(NULL, 2147483651::bigint, 2147483650::bigint, 2147483652::bigint,2147483651::bigint,9999999999::bigint);

-- For type 'numeric'
select decode(12.001::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4));
select decode(12.003::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4));
select decode(12.001::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),999999.9999::numeric(10,4));
select decode(12.003::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),999999.9999::numeric(10,4));

select decode(12.001::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),12.002::numeric(5,3),214748.3651::numeric(10,4));
select decode(12.004::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),12.002::numeric(5,3),214748.3651::numeric(10,4));
select decode(12.001::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),12.002::numeric(5,3),214748.3651::numeric(10,4),999999.9999::numeric(10,4));
select decode(12.004::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),12.002::numeric(5,3),214748.3651::numeric(10,4),999999.9999::numeric(10,4));

select decode(12.001::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),12.002::numeric(5,3),214748.3651::numeric(10,4), 12.003::numeric(5,3), 214748.3652::numeric(10,4));
select decode(12.004::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),12.002::numeric(5,3),214748.3651::numeric(10,4), 12.003::numeric(5,3), 214748.3652::numeric(10,4));
select decode(12.001::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),12.002::numeric(5,3),214748.3651::numeric(10,4), 12.003::numeric(5,3), 214748.3652::numeric(10,4),999999.9999::numeric(10,4));
select decode(12.004::numeric(5,3), 12.001::numeric(5,3),214748.3650::numeric(10,4),12.002::numeric(5,3),214748.3651::numeric(10,4), 12.003::numeric(5,3), 214748.3652::numeric(10,4),999999.9999::numeric(10,4));

select decode(NULL, 12.001::numeric(5,3), 214748.3650::numeric(10,4), NULL,214748.3651::numeric(10,4));
select decode(NULL, 12.001::numeric(5,3), 214748.3650::numeric(10,4), 12.002::numeric(5,3),214748.3651::numeric(10,4));
select decode(NULL, 12.001::numeric(5,3), 214748.3650::numeric(10,4), NULL,214748.3651::numeric(10,4),999999.9999::numeric(10,4));
select decode(NULL, 12.001::numeric(5,3), 214748.3650::numeric(10,4), 12.002::numeric(5,3),214748.3651::numeric(10,4),999999.9999::numeric(10,4));

--For type 'date'
select decode('2020-01-01'::date, '2020-01-01'::date,'2012-12-20'::date);
select decode('2020-01-03'::date, '2020-01-01'::date,'2012-12-20'::date);
select decode('2020-01-01'::date, '2020-01-01'::date,'2012-12-20'::date,'2012-12-21'::date);
select decode('2020-01-03'::date, '2020-01-01'::date,'2012-12-20'::date,'2012-12-21'::date);

select decode('2020-01-01'::date, '2020-01-01'::date,'2012-12-20'::date,'2020-01-02'::date,'2012-12-21'::date);
select decode('2020-01-04'::date, '2020-01-01'::date,'2012-12-20'::date,'2020-01-02'::date,'2012-12-21'::date);
select decode('2020-01-01'::date, '2020-01-01'::date,'2012-12-20'::date,'2020-01-02'::date,'2012-12-21'::date,'2012-12-31'::date);
select decode('2020-01-04'::date, '2020-01-01'::date,'2012-12-20'::date,'2020-01-02'::date,'2012-12-21'::date,'2012-12-31'::date);

select decode('2020-01-01'::date, '2020-01-01'::date,'2012-12-20'::date,'2020-01-02'::date,'2012-12-21'::date, '2020-01-03'::date, '2012-12-31'::date);
select decode('2020-01-04'::date, '2020-01-01'::date,'2012-12-20'::date,'2020-01-02'::date,'2012-12-21'::date, '2020-01-03'::date, '2012-12-31'::date);
select decode('2020-01-01'::date, '2020-01-01'::date,'2012-12-20'::date,'2020-01-02'::date,'2012-12-21'::date, '2020-01-03'::date, '2012-12-31'::date,'2013-01-01'::date);
select decode('2020-01-04'::date, '2020-01-01'::date,'2012-12-20'::date,'2020-01-02'::date,'2012-12-21'::date, '2020-01-03'::date, '2012-12-31'::date,'2013-01-01'::date);

select decode(NULL, '2020-01-01'::date, '2012-12-20'::date, NULL,'2012-12-21'::date);
select decode(NULL, '2020-01-01'::date, '2012-12-20'::date, '2020-01-02'::date,'2012-12-21'::date);
select decode(NULL, '2020-01-01'::date, '2012-12-20'::date, NULL,'2012-12-21'::date,'2012-12-31'::date);
select decode(NULL, '2020-01-01'::date, '2012-12-20'::date, '2020-01-02'::date,'2012-12-21'::date,'2012-12-31'::date);

-- For type 'time'
select decode('01:00:01'::time, '01:00:01'::time,'09:00:00'::time);
select decode('01:00:03'::time, '01:00:01'::time,'09:00:00'::time);
select decode('01:00:01'::time, '01:00:01'::time,'09:00:00'::time,'00:00:00'::time);
select decode('01:00:03'::time, '01:00:01'::time,'09:00:00'::time,'00:00:00'::time);

select decode('01:00:01'::time, '01:00:01'::time,'09:00:00'::time,'01:00:02'::time,'12:00:00'::time);
select decode('01:00:04'::time, '01:00:01'::time,'09:00:00'::time,'01:00:02'::time,'12:00:00'::time);
select decode('01:00:01'::time, '01:00:01'::time,'09:00:00'::time,'01:00:02'::time,'12:00:00'::time,'00:00:00'::time);
select decode('01:00:04'::time, '01:00:01'::time,'09:00:00'::time,'01:00:01'::time,'12:00:00'::time,'00:00:00'::time);

select decode('01:00:01'::time, '01:00:01'::time,'09:00:00'::time,'01:00:02'::time,'12:00:00'::time, '01:00:03'::time, '15:00:00'::time);
select decode('01:00:04'::time, '01:00:01'::time,'09:00:00'::time,'01:00:02'::time,'12:00:00'::time, '01:00:03'::time, '15:00:00'::time);
select decode('01:00:01'::time, '01:00:01'::time,'09:00:00'::time,'01:00:02'::time,'12:00:00'::time, '01:00:03'::time, '15:00:00'::time,'00:00:00'::time);
select decode('01:00:04'::time, '01:00:01'::time,'09:00:00'::time,'01:00:02'::time,'12:00:00'::time, '01:00:03'::time, '15:00:00'::time,'00:00:00'::time);

select decode(NULL, '01:00:01'::time, '09:00:00'::time, NULL,'12:00:00'::time);
select decode(NULL, '01:00:01'::time, '09:00:00'::time, '01:00:02'::time,'12:00:00'::time);
select decode(NULL, '01:00:01'::time, '09:00:00'::time, NULL,'12:00:00'::time,'00:00:00'::time);
select decode(NULL, '01:00:01'::time, '09:00:00'::time, '01:00:02'::time,'12:00:00'::time,'00:00:00'::time);

-- For type 'timestamp'
select decode('2020-01-01 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp);
select decode('2020-01-03 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp);
select decode('2020-01-01 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2012-12-20 00:00:00'::timestamp);
select decode('2020-01-03 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2012-12-20 00:00:00'::timestamp);

select decode('2020-01-01 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp);
select decode('2020-01-04 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp);
select decode('2020-01-01 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp,'2012-12-20 00:00:00'::timestamp);
select decode('2020-01-04 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp,'2012-12-20 00:00:00'::timestamp);

select decode('2020-01-01 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp, '2020-01-03 01:00:01'::timestamp, '2012-12-20 15:00:00'::timestamp);
select decode('2020-01-04 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp, '2020-01-03 01:00:01'::timestamp, '2012-12-20 15:00:00'::timestamp);
select decode('2020-01-01 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp, '2020-01-03 01:00:01'::timestamp, '2012-12-20 15:00:00'::timestamp,'2012-12-20 00:00:00'::timestamp);
select decode('2020-01-04 01:00:01'::timestamp, '2020-01-01 01:00:01'::timestamp,'2012-12-20 09:00:00'::timestamp,'2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp, '2020-01-03 01:00:01'::timestamp, '2012-12-20 15:00:00'::timestamp,'2012-12-20 00:00:00'::timestamp);

select decode(NULL, '2020-01-01 01:00:01'::timestamp, '2012-12-20 09:00:00'::timestamp, NULL,'2012-12-20 12:00:00'::timestamp);
select decode(NULL, '2020-01-01 01:00:01'::timestamp, '2012-12-20 09:00:00'::timestamp, '2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp);
select decode(NULL, '2020-01-01 01:00:01'::timestamp, '2012-12-20 09:00:00'::timestamp, NULL,'2012-12-20 12:00:00'::timestamp,'2012-12-20 00:00:00'::timestamp);
select decode(NULL, '2020-01-01 01:00:01'::timestamp, '2012-12-20 09:00:00'::timestamp, '2020-01-02 01:00:01'::timestamp,'2012-12-20 12:00:00'::timestamp,'2012-12-20 00:00:00'::timestamp);

-- For type 'timestamptz'
select decode('2020-01-01 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz);
select decode('2020-01-03 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz);
select decode('2020-01-01 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2012-12-20 00:00:00-08'::timestamptz);
select decode('2020-01-03 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2012-12-20 00:00:00-08'::timestamptz);

select decode('2020-01-01 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz);
select decode('2020-01-04 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz);
select decode('2020-01-01 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz,'2012-12-20 00:00:00-08'::timestamptz);
select decode('2020-01-04 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz,'2012-12-20 00:00:00-08'::timestamptz);

select decode('2020-01-01 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz, '2020-01-03 01:00:01-08'::timestamptz, '2012-12-20 15:00:00-08'::timestamptz);
select decode('2020-01-04 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz, '2020-01-03 01:00:01-08'::timestamptz, '2012-12-20 15:00:00-08'::timestamptz);
select decode('2020-01-01 01:00:01-08'::timestamptz, '2020-01-01 01:00:01-08'::timestamptz,'2012-12-20 09:00:00-08'::timestamptz,'2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz, '2020-01-03 01:00:01-08'::timestamptz, '2012-12-20 15:00:00-08'::timestamptz,'2012-12-20 00:00:00-08'::timestamptz);
select decode(4, 1,'2012-12-20 09:00:00-08'::timestamptz,2,'2012-12-20 12:00:00-08'::timestamptz, 3, '2012-12-20 15:00:00-08'::timestamptz,'2012-12-20 00:00:00-08'::timestamptz);

select decode(NULL, '2020-01-01 01:00:01-08'::timestamptz, '2012-12-20 09:00:00-08'::timestamptz, NULL,'2012-12-20 12:00:00-08'::timestamptz);
select decode(NULL, '2020-01-01 01:00:01-08'::timestamptz, '2012-12-20 09:00:00-08'::timestamptz, '2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz);
select decode(NULL, '2020-01-01 01:00:01-08'::timestamptz, '2012-12-20 09:00:00-08'::timestamptz, NULL,'2012-12-20 12:00:00-08'::timestamptz,'2012-12-20 00:00:00-08'::timestamptz);
select decode(NULL, '2020-01-01 01:00:01-08'::timestamptz, '2012-12-20 09:00:00-08'::timestamptz, '2020-01-02 01:00:01-08'::timestamptz,'2012-12-20 12:00:00-08'::timestamptz,'2012-12-20 00:00:00-08'::timestamptz);


--Test case to check if decode accepts other expressions as a key

CREATE OR REPLACE FUNCTION five() RETURNS integer AS $$
BEGIN
	RETURN 5;
END; 
$$ LANGUAGE plpgsql;

select decode(five(), 1, 'one', 2, 'two', 5, 'five');

DROP FUNCTION five();

-- Test case to check duplicate keys in search list
select decode(1, 1, 'one', 2, 'two', 1, 'one-again') = 'one';

/* Test case to check explicit type casting of keys in search list in 
 * case of ambiguous key (1st argument) provided.
 */

-- 1) succeed and return 'result-1'
select decode('2012-01-01', '2012-01-01'::date,'result-1','2012-01-02', 'result-2');
select decode('2012-01-01', '2012-01-01', 'result-1', '2012-02-01'::date, 'result-2');

-- 2) fails and throws error: 'ERROR:  could not determine polymorphic type 
-- because input has type "unknown"'
select decode('2012-01-01', '2012-01-01', 23, '2012-01-02', 24);

select PLVstr.rvrs ('Jumping Jack Flash') ='hsalF kcaJ gnipmuJ';
select PLVstr.rvrs ('Jumping Jack Flash', 9) = 'hsalF kcaJ';
select PLVstr.rvrs ('Jumping Jack Flash', 4, 6) = 'nip';
select PLVstr.lstrip ('*val1|val2|val3|*', '*') = 'val1|val2|val3|*';
select PLVstr.lstrip (',,,val1,val2,val3,', ',', 3)= 'val1,val2,val3,';
select PLVstr.lstrip ('WHERE WHITE = ''FRONT'' AND COMP# = 1500', 'WHERE ') = 'WHITE = ''FRONT'' AND COMP# = 1500';
select plvstr.left('Příliš žluťoučký kůň',4) = pg_catalog.substr('Příl', 1, 4);

select pos,token from plvlex.tokens('select * from a.b.c join d ON x=y', true, true);

SET lc_numeric TO 'C';
select to_char(22);
select to_char(-44444);
select to_char(1234567890123456::bigint);
select to_char(123.456::real);
select to_char(1234.5678::double precision);
select to_char(12345678901234567890::numeric);
select to_char(1234567890.12345);
select to_char('4.00'::numeric);
select to_char('4.0010'::numeric);

SELECT to_number('123'::text);
SELECT to_number('123.456'::text);

SELECT to_date('2009-01-02');

SELECT bitand(5,1), bitand(5,2), bitand(5,4);
SELECT sinh(1.570796), cosh(1.570796), tanh(4);
SELECT nanvl(12345, 1), nanvl('NaN', 1);
SELECT nanvl(12345::float4, 1), nanvl('NaN'::float4, 1);
SELECT nanvl(12345::float8, 1), nanvl('NaN'::float8, 1);
SELECT nanvl(12345::numeric, 1), nanvl('NaN'::numeric, 1);

select dbms_assert.enquote_literal('some text '' some text');
select dbms_assert.enquote_name('''"AAA');
select dbms_assert.enquote_name('''"AAA', false);
select dbms_assert.noop('some string');
select dbms_assert.qualified_sql_name('aaa.bbb.ccc."aaaa""aaa"');
select dbms_assert.qualified_sql_name('aaa.bbb.cc%c."aaaa""aaa"');
select dbms_assert.schema_name('dbms_assert');
select dbms_assert.schema_name('jabadabado');
select dbms_assert.simple_sql_name('"Aaa dghh shsh"');
select dbms_assert.simple_sql_name('ajajaj -- ajaj');
select dbms_assert.object_name('pg_catalog.pg_class');
select dbms_assert.object_name('dbms_assert.fooo');

select plunit.assert_true(NULL);
select plunit.assert_true(1 = 2);
select plunit.assert_true(1 = 2, 'one is not two');
select plunit.assert_true(1 = 1);
select plunit.assert_false(1 = 1);
select plunit.assert_false(1 = 1, 'trap is open');
select plunit.assert_false(NULL);
select plunit.assert_null(current_date);
select plunit.assert_null(NULL::date);
select plunit.assert_not_null(current_date);
select plunit.assert_not_null(NULL::date);
select plunit.assert_equals('Pavel','Pa'||'vel');
select plunit.assert_equals(current_date, current_date + 1, 'diff dates');
select plunit.assert_equals(10.2, 10.3, 0.5);
select plunit.assert_equals(10.2, 10.3, 0.01, 'attention some diff');
select plunit.assert_not_equals(current_date, current_date + 1, 'yestarday is today');
select plunit.fail();
select plunit.fail('custom exception');

SELECT dump('Yellow dog'::text) ~ E'^Typ=25 Len=(\\d+): \\d+(,\\d+)*$' AS t;
SELECT dump('Yellow dog'::text, 10) ~ E'^Typ=25 Len=(\\d+): \\d+(,\\d+)*$' AS t;
SELECT dump('Yellow dog'::text, 17) ~ E'^Typ=25 Len=(\\d+): .(,.)*$' AS t;
SELECT dump(10::int2) ~ E'^Typ=21 Len=2: \\d+(,\\d+){1}$' AS t;
SELECT dump(10::int4) ~ E'^Typ=23 Len=4: \\d+(,\\d+){3}$' AS t;
SELECT dump(10::int8) ~ E'^Typ=20 Len=8: \\d+(,\\d+){7}$' AS t;
SELECT dump(10.23::float4) ~ E'^Typ=700 Len=4: \\d+(,\\d+){3}$' AS t;
SELECT dump(10.23::float8) ~ E'^Typ=701 Len=8: \\d+(,\\d+){7}$' AS t;
SELECT dump(10.23::numeric) ~ E'^Typ=1700 Len=(\\d+): \\d+(,\\d+)*$' AS t;
SELECT dump('2008-10-10'::date) ~ E'^Typ=1082 Len=4: \\d+(,\\d+){3}$' AS t;
SELECT dump('2008-10-10'::timestamp) ~ E'^Typ=1114 Len=8: \\d+(,\\d+){7}$' AS t;
SELECT dump('2009-10-10'::timestamp) ~ E'^Typ=1114 Len=8: \\d+(,\\d+){7}$' AS t;

-- Tests for to_multi_byte
SELECT to_multi_byte('123$test');
-- Check internal representation difference
SELECT octet_length('abc');
SELECT octet_length(to_multi_byte('abc'));

-- Tests for round(TIMESTAMP WITH TIME ZONE)
select round(TIMESTAMP WITH TIME ZONE'12/08/1990 05:35:25','YEAR') = '1991-01-01 00:00:00';
select round(TIMESTAMP WITH TIME ZONE'05/08/1990 05:35:25','Q') = '1990-04-01 00:00:00';
select round(TIMESTAMP WITH TIME ZONE'12/08/1990 05:35:25','MONTH') = '1990-12-01 00:00:00';
select round(TIMESTAMP WITH TIME ZONE'12/08/1990 05:35:25','DDD') = '1990-12-08 00:00:00';
select round(TIMESTAMP WITH TIME ZONE'12/08/1990 05:35:25','DAY') = '1990-12-09 00:00:00';
select round(TIMESTAMP WITH TIME ZONE'12/08/1990 05:35:25','hh') = '1990-12-08 06:00:00';
select round(TIMESTAMP WITH TIME ZONE'12/08/1990 05:35:25','mi') = '1990-12-08 05:35:00';

-- Tests for to_date
SET DATESTYLE TO SQL, MDY;
SELECT to_date('2009-01-02');
select to_date('January 8,1999');
SET DATESTYLE TO POSTGRES, MDY;
select to_date('1999-01-08');
select to_date('1/12/1999');
SET DATESTYLE TO SQL, DMY;
select to_date('01/02/03');
select to_date('1999-Jan-08');
select to_date('Jan-08-1999');
select to_date('08-Jan-1999');
SET DATESTYLE TO ISO, YMD;
select to_date('99-Jan-08');
SET DATESTYLE TO ISO, DMY;
select to_date('08-Jan-99');
select to_date('Jan-08-99');
select to_date('19990108');
select to_date('990108');
select to_date('J2451187');
