\set ECHO none
SET client_min_messages = warning;
SET DATESTYLE TO ISO;
SET client_encoding = utf8;
\set ECHO all

SET search_path TO public, oracle;

--
-- test built-in date type oracle compatibility functions
--

SELECT add_months (date '2003-08-01', 3);
SELECT add_months (date '2003-08-01', -3);
SELECT add_months (date '2003-08-21', -3);
SELECT add_months (date '2003-01-31', 1);
SELECT add_months (date '2008-02-28', 1);
SELECT add_months (date '2008-02-29', 1);
SELECT add_months (date '2008-01-31', 12);
SELECT add_months (date '2008-01-31', -12);
SELECT add_months (date '2008-01-31', 95903);
SELECT add_months (date '2008-01-31', -80640);
SELECT add_months (date '03-21-2008',3);
SELECT add_months (date '21-MAR-2008',3);
SELECT add_months (date '21-MAR-08',3);
SELECT add_months (date '2008-MAR-21',3);
SELECT add_months (date 'March 21,2008',3);
SELECT add_months(date '03/21/2008',3);
SELECT add_months(date '20080321',3);
SELECT add_months(date '080321',3);

SELECT add_months ('2003-08-01 10:12:21', 3);
SELECT add_months ('2003-08-01 10:21:21', -3);
SELECT add_months ('2003-08-21 12:21:21', -3);
SELECT add_months ('2003-01-31 01:12:45', 1);
SELECT add_months ('2008-02-28 02:12:12', 1);
SELECT add_months ('2008-02-29 12:12:12', 1);
SELECT add_months ('2008-01-31 11:11:21', 12);
SELECT add_months ('2008-01-31 11:21:21', -12);
SELECT add_months ('2008-01-31 12:12:12', 95903);
SELECT add_months ('2008-01-31 11:32:12', -80640);
SELECT add_months ('03-21-2008 08:12:22',3);
SELECT add_months ('21-MAR-2008 06:02:12',3);
SELECT add_months ('21-MAR-08 12:11:22',3);
SELECT add_months ('2008-MAR-21 11:32:43',3);
SELECT add_months ('March 21,2008 12:32:12',3);
SELECT add_months('03/21/2008 12:32:12',3);
SELECT add_months('20080321 123244',3);
SELECT add_months('080321 121212',3);

SELECT last_day(to_date('2003/03/15', 'yyyy/mm/dd'));
SELECT last_day(to_date('2003/02/03', 'yyyy/mm/dd'));
SELECT last_day(to_date('2004/02/03', 'yyyy/mm/dd'));
SELECT last_day(date '1900-02-01');
SELECT last_day(date '2000-02-01');
SELECT last_day(date '2007-02-01');
SELECT last_day(date '2008-02-01');

SET search_path TO oracle,"$user", public, pg_catalog;
SELECT last_day(to_date('2003/03/15 11:12:21', 'yyyy/mm/dd hh:mi:ss'));
SELECT last_day(to_date('2003/02/03 10:21:32', 'yyyy/mm/dd hh:mi:ss'));
SELECT last_day(to_date('2004/02/03 11:32:12', 'yyyy/mm/dd hh:mi:ss'));
SELECT last_day('1900-02-01 12:12:11');
SELECT last_day('2000-02-01 121143');
SELECT last_day('2007-02-01 12:21:33');
SELECT last_day('2008-02-01 121212');

SET search_path TO public, oracle;
SELECT next_day (date '2003-08-01', 'TUESDAY');
SELECT next_day (date '2003-08-06', 'WEDNESDAY');
SELECT next_day (date '2003-08-06', 'SUNDAY');
SELECT next_day (date '2008-01-01', 'sun');
SELECT next_day (date '2008-01-01', 'sunAAA');
SELECT next_day (date '2008-01-01', 1);
SELECT next_day (date '2008-01-01', 7);

SELECT next_day ('2003-08-01 111211', 'TUESDAY');
SELECT next_day ('2003-08-06 10:11:43', 'WEDNESDAY');
SELECT next_day ('2003-08-06 11:21:21', 'SUNDAY');
SELECT next_day ('2008-01-01 111343', 'sun');
SELECT next_day ('2008-01-01 121212', 'sunAAA');
SELECT next_day ('2008-01-01 111213', 1);
SELECT next_day ('2008-01-01 11:12:13', 7);

SELECT months_between (to_date ('2003/01/01', 'yyyy/mm/dd'), to_date ('2003/03/14', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/07/01', 'yyyy/mm/dd'), to_date ('2003/03/14', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/07/02', 'yyyy/mm/dd'), to_date ('2003/07/02', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/08/02', 'yyyy/mm/dd'), to_date ('2003/06/02', 'yyyy/mm/dd'));
SELECT months_between ('2007-02-28', '2007-04-30');
SELECT months_between ('2008-01-31', '2008-02-29');
SELECT months_between ('2008-02-29', '2008-03-31');
SELECT months_between ('2008-02-29', '2008-04-30');
SELECT trunc(months_between('21-feb-2008', '2008-02-29'));

SELECT months_between (to_date ('2003/01/01 12:12:12', 'yyyy/mm/dd h24:mi:ss'), to_date ('2003/03/14 11:11:11', 'yyyy/mm/dd h24:mi:ss'));
SELECT months_between (to_date ('2003/07/01 10:11:11', 'yyyy/mm/dd h24:mi:ss'), to_date ('2003/03/14 10:12:12', 'yyyy/mm/dd h24:mi:ss'));
SELECT months_between (to_date ('2003/07/02 11:21:21', 'yyyy/mm/dd h24:mi:ss'), to_date ('2003/07/02 11:11:11', 'yyyy/mm/dd h24:mi:ss'));
SELECT months_between (to_timestamp ('2003/08/02 10:11:12', 'yyyy/mm/dd h24:mi:ss'), to_date ('2003/06/02 10:10:11', 'yyyy/mm/dd h24:mi:ss'));
SELECT months_between ('2007-02-28 111111', '2007-04-30 112121');
SELECT months_between ('2008-01-31 11:32:11', '2008-02-29 11:12:12');
SELECT months_between ('2008-02-29 10:11:13', '2008-03-31 10:12:11');
SELECT months_between ('2008-02-29 111111', '2008-04-30 12:12:12');
SELECT trunc(months_between('21-feb-2008 12:11:11', '2008-02-29 11:11:11'));

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

SET search_path TO oracle,"$user", public, pg_catalog;
select next_day(to_date('01-Aug-03 101111', 'DD-MON-YY h24miss'), 'TUESDAY') = to_date ('05-Aug-03 101111', 'DD-MON-YY h24miss');
select next_day(to_date('06-Aug-03 10:12:13', 'DD-MON-YY H24:MI:SS'), 'WEDNESDAY') = to_date ('13-Aug-03 10:12:13', 'DD-MON-YY H24:MI:SS');
select next_day(to_date('06-Aug-03 11:11:11', 'DD-MON-YY HH:MI:SS'), 'SUNDAY') = to_date ('10-Aug-03 11:11:11', 'DD-MON-YY HH:MI:SS');
SET search_path TO public,oracle;

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
select 0 =  instr('ab;cdx', ';', 0);
select oracle.substr('This is a test', 6, 2) = 'is';
select oracle.substr('This is a test', 6) =  'is a test';
select oracle.substr('TechOnTheNet', 1, 4) =  'Tech';
select oracle.substr('TechOnTheNet', -3, 3) =  'Net';
select oracle.substr('TechOnTheNet', -6, 3) =  'The';
select oracle.substr('TechOnTheNet', -8, 2) =  'On';

set orafce.using_substring_zero_width_in_substr TO orafce;
select oracle.substr('TechOnTheNet', -8, 0) =  '';

set orafce.using_substring_zero_width_in_substr TO oracle;
select oracle.substr('TechOnTheNet', -8, 0) is null;

set orafce.using_substring_zero_width_in_substr TO warning_oracle;
select oracle.substr('TechOnTheNet', -8, 0) is null;

set orafce.using_substring_zero_width_in_substr TO default;
select oracle.substr('TechOnTheNet', -8, 0) is null;

select oracle.substr('TechOnTheNet', -8, -1) =  '';
select oracle.substr(1234567,3.6::smallint)='4567';
select oracle.substr(1234567,3.6::int)='4567';
select oracle.substr(1234567,3.6::bigint)='4567';
select oracle.substr(1234567,3.6::numeric)='34567';
select oracle.substr(1234567,-1)='7';
select oracle.substr(1234567,3.6::smallint,2.6)='45';
select oracle.substr(1234567,3.6::smallint,2.6::smallint)='456';
select oracle.substr(1234567,3.6::smallint,2.6::int)='456';
select oracle.substr(1234567,3.6::smallint,2.6::bigint)='456';
select oracle.substr(1234567,3.6::smallint,2.6::numeric)='45';
select oracle.substr(1234567,3.6::int,2.6::smallint)='456';
select oracle.substr(1234567,3.6::int,2.6::int)='456';
select oracle.substr(1234567,3.6::int,2.6::bigint)='456';
select oracle.substr(1234567,3.6::int,2.6::numeric)='45';
select oracle.substr(1234567,3.6::bigint,2.6::smallint)='456';
select oracle.substr(1234567,3.6::bigint,2.6::int)='456';
select oracle.substr(1234567,3.6::bigint,2.6::bigint)='456';
select oracle.substr(1234567,3.6::bigint,2.6::numeric)='45';
select oracle.substr(1234567,3.6::numeric,2.6::smallint)='345';
select oracle.substr(1234567,3.6::numeric,2.6::int)='345';
select oracle.substr(1234567,3.6::numeric,2.6::bigint)='345';
select oracle.substr(1234567,3.6::numeric,2.6::numeric)='34';
select oracle.substr('abcdef'::varchar,3.6::smallint)='def';
select oracle.substr('abcdef'::varchar,3.6::int)='def';
select oracle.substr('abcdef'::varchar,3.6::bigint)='def';
select oracle.substr('abcdef'::varchar,3.6::numeric)='cdef';
select oracle.substr('abcdef'::varchar,3.5::int,3.5::int)='def';
select oracle.substr('abcdef'::varchar,3.5::numeric,3.5::numeric)='cde';
select oracle.substr('abcdef'::varchar,3.5::numeric,3.5::int)='cdef';
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
select nvl(NULL::int, 2);
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

select PLVstr.rvrs ('Jumping Jack Flash') ='hsalF kcaJ gnipmuJ';
select PLVstr.rvrs ('Jumping Jack Flash', 9) = 'hsalF kcaJ';
select PLVstr.rvrs ('Jumping Jack Flash', 4, 6) = 'nip';
select PLVstr.rvrs (NULL, 10, 20);
select PLVstr.rvrs ('alphabet', -2, -5);
select PLVstr.rvrs ('alphabet', -2);
select PLVstr.rvrs ('alphabet', 2, 200);
select PLVstr.rvrs ('alphabet', 20, 200);
select PLVstr.lstrip ('*val1|val2|val3|*', '*') = 'val1|val2|val3|*';
select PLVstr.lstrip (',,,val1,val2,val3,', ',', 3)= 'val1,val2,val3,';
select PLVstr.lstrip ('WHERE WHITE = ''FRONT'' AND COMP# = 1500', 'WHERE ') = 'WHITE = ''FRONT'' AND COMP# = 1500';
select plvstr.left('Příliš žluťoučký kůň',4) = pg_catalog.substr('Příl', 1, 4);

select pos,token from plvlex.tokens('select * from a.b.c join d ON x=y', true, true);

SET lc_numeric TO 'C';
select to_char(22);
select to_char(99::smallint);
select to_char(-44444);
select to_char(1234567890123456::bigint);
select to_char(123.456::real);
select to_char(1234.5678::double precision);
select to_char(12345678901234567890::numeric);
select to_char(1234567890.12345);
select to_char('4.00'::numeric);
select to_char('4.0010'::numeric);

select to_char('-44444');
select to_char('1234567890123456');
select to_char('123.456');
select to_char('123abc');
select to_char('你好123@$%abc');
select to_char('1234567890123456789012345678901234567890123456789012345678901234567890');
select to_char('');
select to_char(' ');
select to_char(null);


SELECT to_number('123'::text);
SELECT to_number('123.456'::text);
SELECT to_number(123);
SELECT to_number(123::smallint);
SELECT to_number(123::int);
SELECT to_number(123::bigint);
SELECT to_number(123::numeric);
SELECT to_number(123.456);
SELECT to_number(1210.73, 9999.99);
SELECT to_number(1210::smallint, 9999::smallint);
SELECT to_number(1210::int, 9999::int);
SELECT to_number(1210::bigint, 9999::bigint);
SELECT to_number(1210.73::numeric, 9999.99::numeric);

SELECT to_date('2009-01-02');

SELECT bitand(5,1), bitand(5,2), bitand(5,4);
SELECT sinh(1.570796)::numeric(10, 8), cosh(1.570796)::numeric(10, 8), tanh(4)::numeric(10, 8);
SELECT nanvl(12345, 1), nanvl('NaN', 1);
SELECT nanvl(12345::float4, 1), nanvl('NaN'::float4, 1);
SELECT nanvl(12345::float8, 1), nanvl('NaN'::float8, 1);
SELECT nanvl(12345::numeric, 1), nanvl('NaN'::numeric, 1);
SELECT nanvl(12345, '1'::varchar), nanvl('NaN', 1::varchar);
SELECT nanvl(12345::float4, '1'::varchar), nanvl('NaN'::float4, '1'::varchar);
SELECT nanvl(12345::float8, '1'::varchar), nanvl('NaN'::float8, '1'::varchar);
SELECT nanvl(12345::numeric, '1'::varchar), nanvl('NaN'::numeric, '1'::varchar);
SELECT nanvl(12345, '1'::char), nanvl('NaN', 1::char);
SELECT nanvl(12345::float4, '1'::char), nanvl('NaN'::float4, '1'::char);
SELECT nanvl(12345::float8, '1'::char), nanvl('NaN'::float8, '1'::char);
SELECT nanvl(12345::numeric, '1'::char), nanvl('NaN'::numeric, '1'::char);

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

select dbms_assert.enquote_literal(NULL);
select dbms_assert.enquote_name(NULL);
select dbms_assert.enquote_name(NULL, false);
select dbms_assert.noop(NULL);
select dbms_assert.qualified_sql_name(NULL);
select dbms_assert.qualified_sql_name(NULL);
select dbms_assert.schema_name(NULL);
select dbms_assert.schema_name(NULL);
select dbms_assert.simple_sql_name(NULL);
select dbms_assert.simple_sql_name(NULL);
select dbms_assert.object_name(NULL);
select dbms_assert.object_name(NULL);

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

-- Tests for to_single_byte
SELECT to_single_byte('123$test');
SELECT to_single_byte('１２３＄ｔｅｓｔ');
-- Check internal representation difference
SELECT octet_length('ａｂｃ');
SELECT octet_length(to_single_byte('ａｂｃ'));

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
set orafce.nls_date_format='YY-MonDD HH24:MI:SS';
select to_date('14-Jan08 11:44:49+05:30');
set orafce.nls_date_format='YY-DDMon HH24:MI:SS';
select to_date('14-08Jan 11:44:49+05:30');
set orafce.nls_date_format='DDMMYYYY HH24:MI:SS';
select to_date('21052014 12:13:44+05:30');
set orafce.nls_date_format='DDMMYY HH24:MI:SS';
select to_date('210514 12:13:44+05:30');
set orafce.nls_date_format='DDMMYY HH24:MI:SS.MS';
select oracle.orafce__obsolete_to_date('210514 12:13:44.55');
select oracle.to_date('210514 12:13:44.55');

-- Tests for oracle.to_date(text,text)
SET search_path TO oracle,"$user", public, pg_catalog;
select to_date('2014/04/25 10:13', 'YYYY/MM/DD HH:MI');
select to_date('16-Feb-09 10:11:11', 'DD-Mon-YY HH:MI:SS');
select to_date('02/16/09 04:12:12', 'MM/DD/YY HH24:MI:SS');
select to_date('021609 111213', 'MMDDYY HHMISS');
select to_date('16-Feb-09 11:12:12', 'DD-Mon-YY HH:MI:SS');
select to_date('Feb/16/09 11:21:23', 'Mon/DD/YY HH:MI:SS');
select to_date('February.16.2009 10:11:12', 'Month.DD.YYYY HH:MI:SS');
select to_date('20020315111212', 'yyyymmddhh12miss');
select to_date('January 15, 1989, 11:00 A.M.','Month dd, YYYY, HH:MI A.M.');
select to_date('14-Jan08 11:44:49+05:30' ,'YY-MonDD HH24:MI:SS');
select to_date('14-08Jan 11:44:49+05:30','YY-DDMon HH24:MI:SS');
select to_date('21052014 12:13:44+05:30','DDMMYYYY HH24:MI:SS');
select to_date('210514 12:13:44+05:30','DDMMYY HH24:MI:SS');
SET search_path TO public,oracle;

-- Tests for + operator with DATE and number(smallint,integer,bigint,numeric)
SET search_path TO oracle,"$user", public, pg_catalog;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') + 9::smallint;
SET orafce.nls_date_format='MM-DD-YYYY HH24:MI:SS';
SELECT to_date('07-02-2014 10:08:55') + 9::smallint;
SET orafce.nls_date_format='DD-MM-YYYY HH24:MI:SS';
SELECT to_date('02-07-2014 10:08:55') + 9::smallint;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') + 9;
SELECT to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS') + 9::smallint;
SELECT to_date('02-07-2014 10:08:55','DD-MM-YYYY HH:MI:SS') + 9::smallint;
SELECT to_date('07-02-2014 10:08:55','MM-DD-YYYY HH:MI:SS') + 9::smallint;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') + 9::bigint;
SET orafce.nls_date_format='MM-DD-YYYY HH24:MI:SS';
SELECT to_date('07-02-2014 10:08:55') + 9::bigint;
SET orafce.nls_date_format='DD-MM-YYYY HH24:MI:SS';
SELECT to_date('02-07-2014 10:08:55') + 9::bigint;
SELECT to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS') + 9::bigint;
SELECT to_date('02-07-2014 10:08:55','DD-MM-YYYY HH:MI:SS') + 9::bigint;
SELECT to_date('07-02-2014 10:08:55','MM-DD-YYYY HH:MI:SS') + 9::bigint;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') + 9::integer;
SET orafce.nls_date_format='MM-DD-YYYY HH24:MI:SS';
SELECT to_date('07-02-2014 10:08:55') + 9::integer;
SET orafce.nls_date_format='DD-MM-YYYY HH24:MI:SS';
SELECT to_date('02-07-2014 10:08:55') + 9::integer;
SELECT to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS') + 9::integer;
SELECT to_date('02-07-2014 10:08:55','DD-MM-YYYY HH:MI:SS') + 9::integer;
SELECT to_date('07-02-2014 10:08:55','MM-DD-YYYY HH:MI:SS') + 9::integer;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') + 9::numeric;
SET orafce.nls_date_format='MM-DD-YYYY HH24:MI:SS';
SELECT to_date('07-02-2014 10:08:55') + 9::numeric;
SET orafce.nls_date_format='DD-MM-YYYY HH24:MI:SS';
SELECT to_date('02-07-2014 10:08:55') + 9::numeric;
SELECT to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS') + 9::numeric;
SELECT to_date('02-07-2014 10:08:55','DD-MM-YYYY HH:MI:SS') + 9::numeric;
SELECT to_date('07-02-2014 10:08:55','MM-DD-YYYY HH:MI:SS') + 9::numeric;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-01-01 00:00:00') + 1.5;
SELECT to_date('2014-01-01 00:00:00','yyyy-mm-dd hh24:mi:ss') + 1.5;
SET search_path TO public,oracle;

-- Tests for - operator with DATE and number(smallint,integer,bigint,numeric)
SET search_path TO oracle,"$user", public, pg_catalog;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') - 9::smallint;
SET orafce.nls_date_format='MM-DD-YYYY HH24:MI:SS';
SELECT to_date('07-02-2014 10:08:55') - 9::smallint;
SET orafce.nls_date_format='DD-MM-YYYY HH24:MI:SS';
SELECT to_date('02-07-2014 10:08:55') - 9::smallint;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') - 9;
SELECT to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS') - 9::smallint;
SELECT to_date('02-07-2014 10:08:55','DD-MM-YYYY HH:MI:SS') - 9::smallint;
SELECT to_date('07-02-2014 10:08:55','MM-DD-YYYY HH:MI:SS') - 9::smallint;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') - 9::bigint;
SET orafce.nls_date_format='MM-DD-YYYY HH24:MI:SS';
SELECT to_date('07-02-2014 10:08:55') - 9::bigint;
SET orafce.nls_date_format='DD-MM-YYYY HH24:MI:SS';
SELECT to_date('02-07-2014 10:08:55') - 9::bigint;
SELECT to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS') - 9::bigint;
SELECT to_date('02-07-2014 10:08:55','DD-MM-YYYY HH:MI:SS') - 9::bigint;
SELECT to_date('07-02-2014 10:08:55','MM-DD-YYYY HH:MI:SS') - 9::bigint;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') - 9::integer;
SET orafce.nls_date_format='MM-DD-YYYY HH24:MI:SS';
SELECT to_date('07-02-2014 10:08:55') - 9::integer;
SET orafce.nls_date_format='DD-MM-YYYY HH24:MI:SS';
SELECT to_date('02-07-2014 10:08:55') - 9::integer;
SELECT to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS') - 9::integer;
SELECT to_date('02-07-2014 10:08:55','DD-MM-YYYY HH:MI:SS') - 9::integer;
SELECT to_date('07-02-2014 10:08:55','MM-DD-YYYY HH:MI:SS') - 9::integer;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-07-02 10:08:55') - 9::numeric;
SET orafce.nls_date_format='MM-DD-YYYY HH24:MI:SS';
SELECT to_date('07-02-2014 10:08:55') - 9::numeric;
SET orafce.nls_date_format='DD-MM-YYYY HH24:MI:SS';
SELECT to_date('02-07-2014 10:08:55') - 9::numeric;
SELECT to_date('2014-07-02 10:08:55','YYYY-MM-DD HH:MI:SS') - 9::numeric;
SELECT to_date('02-07-2014 10:08:55','DD-MM-YYYY HH:MI:SS') - 9::numeric;
SELECT to_date('07-02-2014 10:08:55','MM-DD-YYYY HH:MI:SS') - 9::numeric;
SET orafce.nls_date_format='YYYY-MM-DD HH24:MI:SS';
SELECT to_date('2014-01-01 00:00:00') - 1.5;
SELECT to_date('2014-01-01 00:00:00','yyyy-mm-dd hh24:mi:ss') - 1.5;
SET search_path TO public,oracle;

--Tests for oracle.to_char(timestamp)-used to set the DATE output format
SET search_path TO oracle,"$user", public, pg_catalog;
SET orafce.nls_date_format to default;
select oracle.to_char(to_date('19-APR-16 21:41:48'));
set orafce.nls_date_format='YY-MonDD HH24:MI:SS';
select oracle.to_char(to_date('14-Jan08 11:44:49+05:30'));
set orafce.nls_date_format='YY-DDMon HH24:MI:SS';
select oracle.to_char(to_date('14-08Jan 11:44:49+05:30'));
set orafce.nls_date_format='DDMMYYYY HH24:MI:SS';
select oracle.to_char(to_date('21052014 12:13:44+05:30'));
set orafce.nls_date_format='DDMMYY HH24:MI:SS';
select oracle.to_char(to_date('210514 12:13:44+05:30'));
set orafce.nls_date_format='DDMMYYYY HH24:MI:SS';
select oracle.to_char(oracle.to_date('2014/04/25 10:13', 'YYYY/MM/DD HH:MI'));
set orafce.nls_date_format='YY-DDMon HH24:MI:SS';
select oracle.to_char(oracle.to_date('16-Feb-09 10:11:11', 'DD-Mon-YY HH:MI:SS'));
set orafce.nls_date_format='YY-DDMon HH24:MI:SS';
select oracle.to_char(oracle.to_date('02/16/09 04:12:12', 'MM/DD/YY HH24:MI:SS'));
set orafce.nls_date_format='YY-MonDD HH24:MI:SS';
select oracle.to_char(oracle.to_date('021609 111213', 'MMDDYY HHMISS'));
set orafce.nls_date_format='DDMMYYYY HH24:MI:SS';
select oracle.to_char(oracle.to_date('16-Feb-09 11:12:12', 'DD-Mon-YY HH:MI:SS'));
set orafce.nls_date_format='DDMMYYYY HH24:MI:SS';
select oracle.to_char(oracle.to_date('Feb/16/09 11:21:23', 'Mon/DD/YY HH:MI:SS'));
set orafce.nls_date_format='DDMMYYYY HH24:MI:SS';
select oracle.to_char(oracle.to_date('February.16.2009 10:11:12', 'Month.DD.YYYY HH:MI:SS'));
set orafce.nls_date_format='YY-MonDD HH24:MI:SS';
select oracle.to_char(oracle.to_date('20020315111212', 'yyyymmddhh12miss'));
set orafce.nls_date_format='DDMMYYYY HH24:MI:SS';
select oracle.to_char(oracle.to_date('January 15, 1989, 11:00 A.M.','Month dd, YYYY, HH:MI A.M.'));
set orafce.nls_date_format='DDMMYY HH24:MI:SS';
select oracle.to_char(oracle.to_date('14-Jan08 11:44:49+05:30' ,'YY-MonDD HH24:MI:SS'));
set orafce.nls_date_format='DDMMYYYY HH24:MI:SS';
select oracle.to_char(oracle.to_date('14-08Jan 11:44:49+05:30','YY-DDMon HH24:MI:SS'));
set orafce.nls_date_format='YY-MonDD HH24:MI:SS';
select oracle.to_char(oracle.to_date('21052014 12:13:44+05:30','DDMMYYYY HH24:MI:SS'));
set orafce.nls_date_format='DDMMYY HH24:MI:SS';
select oracle.to_char(oracle.to_date('210514 12:13:44+05:30','DDMMYY HH24:MI:SS'));
SET search_path TO public,oracle;

--Tests for oracle.-(oracle.date,oracle.date)
SET search_path TO oracle,"$user", public, pg_catalog;
SELECT (to_date('2014-07-17 11:10:15', 'yyyy-mm-dd hh24:mi:ss') - to_date('2014-02-01 10:00:00', 'yyyy-mm-dd hh24:mi:ss'))::numeric(10,4);
SELECT (to_date('2014-07-17 13:14:15', 'yyyy-mm-dd hh24:mi:ss') - to_date('2014-02-01 10:00:00', 'yyyy-mm-dd hh24:mi:ss'))::numeric(10,4);
SELECT (to_date('07-17-2014 13:14:15', 'mm-dd-yyyy hh24:mi:ss') - to_date('2014-02-01 10:00:00', 'yyyy-mm-dd hh24:mi:ss'))::numeric(10,4);
SELECT (to_date('07-17-2014 13:14:15', 'mm-dd-yyyy hh24:mi:ss') - to_date('2015-02-01 10:00:00', 'yyyy-mm-dd hh24:mi:ss'))::numeric(10,4);
SELECT (to_date('07-17-2014 13:14:15', 'mm-dd-yyyy hh24:mi:ss') - to_date('01-01-2013 10:00:00', 'mm-dd-yyyy hh24:mi:ss'))::numeric(10,4);
SELECT (to_date('17-07-2014 13:14:15', 'dd-mm-yyyy hh24:mi:ss') - to_date('01-01-2013 10:00:00', 'dd--mm-yyyy hh24:mi:ss'))::numeric(10,4);
SELECT (to_date('2014/02/01 10:11:12', 'YYYY/MM/DD hh12:mi:ss') - to_date('2013/02/01 10:11:12', 'YYYY/MM/DD hh12:mi:ss'))::numeric(10,4);
SELECT (to_date('17-Jul-14 10:11:11', 'DD-Mon-YY HH:MI:SS') - to_date('17-Jan-14 00:00:00', 'DD-Mon-YY HH24:MI:SS'))::numeric(10,4);
SELECT (to_date('July.17.2014 10:11:12', 'Month.DD.YYYY HH:MI:SS') - to_date('February.16.2014 10:21:12', 'Month.DD.YYYY HH:MI:SS'))::numeric(10,4);
SELECT (to_date('20140717111211', 'yyyymmddhh12miss') - to_date('20140315111212', 'yyyymmddhh12miss'))::numeric(10,4);
SELECT (to_date('January 15, 1990, 11:00 A.M.','Month dd, YYYY, HH:MI A.M.') - to_date('January 15, 1989, 10:00 A.M.','Month dd, YYYY, HH:MI A.M.'))::numeric(10,4);
SELECT (to_date('14-Jul14 11:44:49' ,'YY-MonDD HH24:MI:SS') - to_date('14-Jan14 12:44:49' ,'YY-MonDD HH24:MI:SS'))::numeric(10,4);
SELECT (to_date('210514 12:13:44','DDMMYY HH24:MI:SS') - to_date('210114 10:13:44','DDMMYY HH24:MI:SS'))::numeric(10,4);
SELECT trunc(to_date('210514 12:13:44','DDMMYY HH24:MI:SS'));
SELECT round(to_date('210514 12:13:44','DDMMYY HH24:MI:SS'));


SET search_path TO public,oracle;

--
-- Note: each Japanese character used below has display width of 2, otherwise 1.
-- Note: each output string is surrounded by '|' for improved readability
--

--
-- test LPAD family of functions
--

/* cases where one or more arguments are of type CHAR */
SELECT '|' || oracle.lpad('あbcd'::char(8), 10) || '|';
SELECT '|' || oracle.lpad('あbcd'::char(8),  5) || '|';
SELECT '|' || oracle.lpad('あbcd'::char(8), 1) || '|';

SELECT '|' || oracle.lpad('あbcd'::char(5), 10, 'xい'::char(3)) || '|';
SELECT '|' || oracle.lpad('あbcd'::char(5),  5, 'xい'::char(3)) || '|';

SELECT '|' || oracle.lpad('あbcd'::char(5), 10, 'xい'::text) || '|';
SELECT '|' || oracle.lpad('あbcd'::char(5), 10, 'xい'::varchar2(5)) || '|';
SELECT '|' || oracle.lpad('あbcd'::char(5), 10, 'xい'::nvarchar2(3)) || '|';

SELECT '|' || oracle.lpad('あbcd'::text, 10, 'xい'::char(3)) || '|';
SELECT '|' || oracle.lpad('あbcd'::text,  5, 'xい'::char(3)) || '|';

SELECT '|' || oracle.lpad('あbcd'::varchar2(5), 10, 'xい'::char(3)) || '|';
SELECT '|' || oracle.lpad('あbcd'::varchar2(5),  5, 'xい'::char(3)) || '|';

SELECT '|' || oracle.lpad('あbcd'::nvarchar2(5), 10, 'xい'::char(3)) || '|';
SELECT '|' || oracle.lpad('あbcd'::nvarchar2(5),  5, 'xい'::char(3)) || '|';

/* test oracle.lpad(text, int [, text]) */
SELECT '|' || oracle.lpad('あbcd'::text, 10) || '|';
SELECT '|' || oracle.lpad('あbcd'::text,  5) || '|';

SELECT '|' || oracle.lpad('あbcd'::varchar2(10), 10) || '|';
SELECT '|' || oracle.lpad('あbcd'::varchar2(10), 5) || '|';

SELECT '|' || oracle.lpad('あbcd'::nvarchar2(10), 10) || '|';
SELECT '|' || oracle.lpad('あbcd'::nvarchar2(10), 5) || '|';

SELECT '|' || oracle.lpad('あbcd'::text, 10, 'xい'::text) || '|';
SELECT '|' || oracle.lpad('あbcd'::text, 10, 'xい'::varchar2(5)) || '|';
SELECT '|' || oracle.lpad('あbcd'::text, 10, 'xい'::nvarchar2(3)) || '|';

SELECT '|' || oracle.lpad('あbcd'::varchar2(5), 10, 'xい'::text) || '|';
SELECT '|' || oracle.lpad('あbcd'::varchar2(5), 10, 'xい'::varchar2(5)) || '|';
SELECT '|' || oracle.lpad('あbcd'::varchar2(5), 10, 'xい'::nvarchar2(5)) || '|';

SELECT '|' || oracle.lpad('あbcd'::nvarchar2(5), 10, 'xい'::text) || '|';
SELECT '|' || oracle.lpad('あbcd'::nvarchar2(5), 10, 'xい'::varchar2(5)) || '|';
SELECT '|' || oracle.lpad('あbcd'::nvarchar2(5), 10, 'xい'::nvarchar2(5)) || '|';

--
-- test RPAD family of functions
--

/* cases where one or more arguments are of type CHAR */
SELECT '|' || oracle.rpad('あbcd'::char(8), 10) || '|';
SELECT '|' || oracle.rpad('あbcd'::char(8),  5) || '|';
SELECT '|' || oracle.rpad('あbcd'::char(8), 1) || '|';

SELECT '|' || oracle.rpad('あbcd'::char(5), 10, 'xい'::char(3)) || '|';
SELECT '|' || oracle.rpad('あbcd'::char(5),  5, 'xい'::char(3)) || '|';

SELECT '|' || oracle.rpad('あbcd'::char(5), 10, 'xい'::text) || '|';
SELECT '|' || oracle.rpad('あbcd'::char(5), 10, 'xい'::varchar2(5)) || '|';
SELECT '|' || oracle.rpad('あbcd'::char(5), 10, 'xい'::nvarchar2(3)) || '|';

SELECT '|' || oracle.rpad('あbcd'::text, 10, 'xい'::char(3)) || '|';
SELECT '|' || oracle.rpad('あbcd'::text,  5, 'xい'::char(3)) || '|';

SELECT '|' || oracle.rpad('あbcd'::varchar2(5), 10, 'xい'::char(3)) || '|';
SELECT '|' || oracle.rpad('あbcd'::varchar2(5),  5, 'xい'::char(3)) || '|';

SELECT '|' || oracle.rpad('あbcd'::nvarchar2(5), 10, 'xい'::char(3)) || '|';
SELECT '|' || oracle.rpad('あbcd'::nvarchar2(5),  5, 'xい'::char(3)) || '|';

/* test oracle.lpad(text, int [, text]) */
SELECT '|' || oracle.rpad('あbcd'::text, 10) || '|';
SELECT '|' || oracle.rpad('あbcd'::text,  5) || '|';

SELECT '|' || oracle.rpad('あbcd'::varchar2(10), 10) || '|';
SELECT '|' || oracle.rpad('あbcd'::varchar2(10), 5) || '|';

SELECT '|' || oracle.rpad('あbcd'::nvarchar2(10), 10) || '|';
SELECT '|' || oracle.rpad('あbcd'::nvarchar2(10), 5) || '|';

SELECT '|' || oracle.rpad('あbcd'::text, 10, 'xい'::text) || '|';
SELECT '|' || oracle.rpad('あbcd'::text, 10, 'xい'::varchar2(5)) || '|';
SELECT '|' || oracle.rpad('あbcd'::text, 10, 'xい'::nvarchar2(3)) || '|';

SELECT '|' || oracle.rpad('あbcd'::varchar2(5), 10, 'xい'::text) || '|';
SELECT '|' || oracle.rpad('あbcd'::varchar2(5), 10, 'xい'::varchar2(5)) || '|';
SELECT '|' || oracle.rpad('あbcd'::varchar2(5), 10, 'xい'::nvarchar2(5)) || '|';

SELECT '|' || oracle.rpad('あbcd'::nvarchar2(5), 10, 'xい'::text) || '|';
SELECT '|' || oracle.rpad('あbcd'::nvarchar2(5), 10, 'xい'::varchar2(5)) || '|';
SELECT '|' || oracle.rpad('あbcd'::nvarchar2(5), 10, 'xい'::nvarchar2(5)) || '|';

--
-- test TRIM family of functions
--

/* test that trailing blanks of CHAR arguments are not removed and are significant */

--
-- LTRIM
--
SELECT '|' || oracle.ltrim(' abcd'::char(10)) || '|' as LTRIM;
SELECT '|' || oracle.ltrim(' abcd'::char(10),'a'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.ltrim(' abcd'::char(10),'a'::text) || '|' as LTRIM;
SELECT '|' || oracle.ltrim(' abcd'::char(10),'a'::varchar2(3)) || '|' as LTRIM;
SELECT '|' || oracle.ltrim(' abcd'::char(10),'a'::nvarchar2(3)) || '|' as LTRIM;

SELECT '|' || oracle.ltrim(' abcd  '::text,'a'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.ltrim(' abcd  '::varchar2(10),'a'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.ltrim(' abcd  '::nvarchar2(10),'a'::char(3)) || '|' as LTRIM;

--
-- RTRIM
--
SELECT '|' || oracle.rtrim(' abcd'::char(10)) || '|' as LTRIM;
SELECT '|' || oracle.rtrim(' abcd'::char(10),'d'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.rtrim(' abcd'::char(10),'d'::text) || '|' as LTRIM;
SELECT '|' || oracle.rtrim(' abcd'::char(10),'d'::varchar2(3)) || '|' as LTRIM;
SELECT '|' || oracle.rtrim(' abcd'::char(10),'d'::nvarchar2(3)) || '|' as LTRIM;

SELECT '|' || oracle.rtrim(' abcd  '::text,'d'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.rtrim(' abcd  '::varchar2(10),'d'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.rtrim(' abcd  '::nvarchar2(10),'d'::char(3)) || '|' as LTRIM;

--
-- BTRIM
--
SELECT '|' || oracle.btrim(' abcd'::char(10)) || '|' as LTRIM;
SELECT '|' || oracle.btrim(' abcd'::char(10),'ad'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.btrim(' abcd'::char(10),'ad'::text) || '|' as LTRIM;
SELECT '|' || oracle.btrim(' abcd'::char(10),'ad'::varchar2(3)) || '|' as LTRIM;
SELECT '|' || oracle.btrim(' abcd'::char(10),'ad'::nvarchar2(3)) || '|' as LTRIM;

SELECT '|' || oracle.btrim(' abcd  '::text,'d'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.btrim(' abcd  '::varchar2(10),'d'::char(3)) || '|' as LTRIM;
SELECT '|' || oracle.btrim(' abcd  '::nvarchar2(10),'d'::char(3)) || '|' as LTRIM;

--
-- test oracle.length()
--

/* test that trailing blanks are not ignored */
SELECT oracle.length('あbb'::char(6));
SELECT oracle.length(''::char(6));


--
-- test plvdate.bizdays_between
--
SELECT plvdate.including_start();
SELECT plvdate.bizdays_between('2016-02-24','2016-02-26');
SELECT plvdate.bizdays_between('2016-02-21','2016-02-27');
SELECT plvdate.include_start(false);
SELECT plvdate.bizdays_between('2016-02-24','2016-02-26');
SELECT plvdate.bizdays_between('2016-02-21','2016-02-27');

SELECT oracle.round(1.234::double precision, 2), oracle.trunc(1.234::double precision, 2);
SELECT oracle.round(1.234::float, 2), oracle.trunc(1.234::float, 2);

--
-- should not fail - fix: Crashes due to insufficent argument checking (#59)
--
select dbms_random.string(null, 42);
select dbms_pipe.create_pipe(null);
select plunit.assert_not_equals(1,2,3);

--
-- lexer text
--
SELECT pos, token, class, mod FROM plvlex.tokens('select * from a.b.c join d on x=y', true, true);

--
-- trigger functions
--

CREATE TABLE trg_test(a varchar, b int, c varchar, d date, e int);

CREATE TRIGGER trg_test_xx BEFORE INSERT OR UPDATE
  ON trg_test FOR EACH ROW EXECUTE PROCEDURE oracle.replace_empty_strings(true);

\pset null ***

INSERT INTO trg_test VALUES('',10, 'AHOJ', NULL, NULL);
INSERT INTO trg_test VALUES('AHOJ', NULL, '', '2020-01-01', 100);

SELECT * FROM trg_test;

DELETE FROM trg_test;

DROP TRIGGER trg_test_xx ON trg_test;

CREATE TRIGGER trg_test_xx BEFORE INSERT OR UPDATE
  ON trg_test FOR EACH ROW EXECUTE PROCEDURE oracle.replace_null_strings();

INSERT INTO trg_test VALUES(NULL, 10, 'AHOJ', NULL, NULL);
INSERT INTO trg_test VALUES('AHOJ', NULL, NULL, '2020-01-01', 100);

SELECT * FROM trg_test;

DROP TABLE trg_test;

SELECT oracle.unistr('\0441\043B\043E\043D');
SELECT oracle.unistr('d\u0061t\U00000061');

-- run-time error
SELECT oracle.unistr('wrong: \db99');
SELECT oracle.unistr('wrong: \db99\0061');
SELECT oracle.unistr('wrong: \+00db99\+000061');
SELECT oracle.unistr('wrong: \+2FFFFF');
SELECT oracle.unistr('wrong: \udb99\u0061');
SELECT oracle.unistr('wrong: \U0000db99\U00000061');
SELECT oracle.unistr('wrong: \U002FFFFF');
----
-- Tests for the greatest/least scalar function
----
-- The PostgreSQL native function returns NULL only if all parameters are nulls
SELECT greatest(2, 6, 8);
SELECT greatest(2, NULL, 8);
SELECT least(2, 6, 8);
SELECT least(2, NULL, 8);

-- The Oracle function returns NULL on NULL input, even a single parameter
SELECT oracle.greatest(2, 6, 8, 4);
SELECT oracle.greatest(2, NULL, 8, 4);
SELECT oracle.least(2, 6, 8, 1);
SELECT oracle.least(2, NULL, 8, 1);

-- Test different data type
SELECT oracle.greatest('A'::text, 'B'::text, 'C'::text, 'D'::text);
SELECT oracle.greatest('A'::bpchar, 'B'::bpchar, 'C'::bpchar, 'D'::bpchar);
SELECT oracle.greatest(1::bigint,2::bigint,3::bigint,4::bigint);
SELECT oracle.greatest(1::integer,2::integer,3::integer,4::integer);
SELECT oracle.greatest(1::smallint,2::smallint,3::smallint,4::smallint);
SELECT oracle.greatest(1.2::numeric,2.4::numeric,2.3::numeric,2.2::numeric);
SELECT oracle.greatest(1.2::double precision,2.4::double precision,2.3::double precision,2.2::double precision);
SELECT oracle.greatest(1.2::real,2.4::real,2.2::real,2.3::real);
SELECT oracle.least('A'::text, 'B'::text, 'C'::text, 'D'::text);
SELECT oracle.least('A'::bpchar, 'B'::bpchar, 'C'::bpchar, 'D'::bpchar);
SELECT oracle.least(1::bigint,2::bigint,3::bigint,4::bigint);
SELECT oracle.least(1::integer,2::integer,3::integer,4::integer);
SELECT oracle.least(1::smallint,2::smallint,3::smallint,4::smallint);
SELECT oracle.least(1.2::numeric,2.4::numeric,2.3::numeric,2.2::numeric);
SELECT oracle.least(1.2::double precision,2.4::double precision,2.3::double precision,2.2::double precision);
SELECT oracle.least(1.2::real,2.4::real,2.2::real,2.3::real);

SELECT i, oracle.greatest(100, 24, 1234, 12, i) FROM generate_series(1,3) g(i);

-- test remainder function
CREATE TABLE testorafce_remainder(v1 int, v2 int);

INSERT INTO testorafce_remainder VALUES(24, 7);
INSERT INTO testorafce_remainder VALUES(24, 6);
INSERT INTO testorafce_remainder VALUES(24, 5);
INSERT INTO testorafce_remainder VALUES(-58, -10);
INSERT INTO testorafce_remainder VALUES(58, 10);
INSERT INTO testorafce_remainder VALUES(58, -10);
INSERT INTO testorafce_remainder VALUES(58, 10);
INSERT INTO testorafce_remainder VALUES(-44, -10);
INSERT INTO testorafce_remainder VALUES(44, 10);
INSERT INTO testorafce_remainder VALUES(44, -10);
INSERT INTO testorafce_remainder VALUES(44, 10);

SELECT v1, v2, oracle.remainder(v1, v2) FROM testorafce_remainder;
SELECT v1, v2, oracle.remainder(v1::smallint, v2::smallint) FROM testorafce_remainder;
SELECT v1, v2, oracle.remainder(v1::bigint, v2::bigint) FROM testorafce_remainder;
SELECT v1, v2, oracle.remainder(v1::numeric, v2::numeric) FROM testorafce_remainder;

DROP TABLE testorafce_remainder;
