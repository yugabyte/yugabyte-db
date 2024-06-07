-- bit
insert into testbit values (1, '001111');
insert into testbit values (2, '110101');
begin;
insert into testbit values (3, '111111');
update testbit set b = '000000', a = 0 where a = 1;
commit;
delete from testbit where a = 2;

-- boolean
insert into testboolean values (1, FALSE);
insert into testboolean values (3, TRUE);
begin;
update testboolean set b = FALSE where a = 3;
commit;
delete from testboolean where a = 1;

-- box
insert into testbox values (1, '(8, 9), (1, 3)');
update testbox set b = '(8, 9), (10, 31)' where a = 1;
delete from testbox where a = 1;
begin;
insert into testbox values (2, '(8, 9), (10, 31)');
commit;

-- bytea
insert into testbytea values (1, E'\\001');
update testbytea set b = E'\\xDEADBEEF' where a = 1;
delete from testbytea where a = 1;
begin;
insert into testbytea values (2, E'\\xDEADBEEF');
commit;

-- cidr
insert into testcidr values (1, '10.1.0.0/16');
update testcidr set b = '12.2.0.0/22' where a = 1;
delete from testcidr where a = 1;
begin;
insert into testcidr values (2, '12.2.0.0/22');
commit;

-- circle
insert into testcircle values (10, '2, 3, 32');
update testcircle set b = '0, 0, 10' where a = 10;
delete from testcircle where a = 10;
begin;
insert into testcircle values (1000, '0, 0, 4');
commit;

-- date
insert into testdate values (1, '2021-09-20');
update testdate set b = '2021-09-29' where a = 1;
insert into testdate values (2, '2000-01-01');
delete from testdate where a = 2;
insert into testdate values (3, '1970-01-01');
update testdate set a = a + 1 where a = 3;

-- double
insert into testdouble values (1, 10.42);
insert into testdouble values (3, 0.5);
begin;
update testdouble set b = 34.56 where a = 5;
commit;
update testdouble set a = a + 1 where a = 3;

-- inet
insert into testinet values (1, '127.0.0.1');
insert into testinet values (2, '0.0.0.0');
insert into testinet values (3, '192.168.1.1');
delete from testinet where a = 3;

-- int
insert into testint values (1, 2);
insert into testint values (3, 4);
begin;
update testint set b = b + 1 where a = 3;
commit;
begin;
insert into testint values (7, 8);
update testint set a = a + 1 where a = 7;
end;
delete from testint where a = 8;

-- json
insert into testjson values (1, '{"first_name":"vaibhav"}');
insert into testjson values (2, '{"last_name":"kushwaha"}');
update testjson set b = '{"name":"vaibhav kushwaha"}' where a = 2;
begin;
delete from testjson where a = 1;
insert into testjson values (3, '{"a":97, "b":"98"}');
commit;

-- jsonb
insert into testjsonb values (1, '{"first_name":"vaibhav"}');
insert into testjsonb values (2, '{"last_name":"kushwaha"}');
update testjsonb set b = '{"name":"vaibhav kushwaha"}' where a = 2;
begin;
delete from testjsonb where a = 1;
insert into testjsonb values (3, '{"a":97, "b":"98"}');
commit;

-- line
insert into testline values (1, '{1, 2, -8}');
update testline set b = '{1, 1, -5}' where a = 1;
delete from testline where a = 1;
begin;
insert into testline values (29, '[(0, 0), (2, 5)]');
commit;

-- lseg
insert into testlseg values (1, '[(0, 0), (2, 4)]');
update testlseg set b = '((-1, -1), (10, -8))' where a = 1;
delete from testlseg where a = 1;
begin;
insert into testlseg values (37, '[(1, 3), (3, 5)]');
commit;

-- macaddr8
insert into testmacaddr8 values (1, '22:00:5c:03:55:08:01:02');
update testmacaddr8 set b = '22:00:5c:04:55:08:01:02' where a = 1;
begin;
insert into testmacaddr8 values (2, '22:00:5c:03:55:08:01:02');
delete from testmacaddr8 where a = 2;
commit;
insert into testmacaddr8 values (3, '22:00:5c:05:55:08:01:02');
delete from testmacaddr8 where a = 3;

-- macaddr
insert into testmacaddr values (1, '2C:54:91:88:C9:E3');
update testmacaddr set b = '2C:54:91:E8:99:D2' where a = 1;
delete from testmacaddr where a = 1;
begin;
insert into testmacaddr values (2, '2C:54:91:E8:99:D2');
commit;

-- money
insert into testmoney values (1, 100.5);
insert into testmoney values (2, 10.12);
begin;
insert into testmoney values (3, 1.23);
update testmoney set b = b - '$10' where a = 1;
commit;
delete from testmoney where a = 2;

-- numeric
insert into testnumeric values (1, 20.5);
insert into testnumeric values (2, 100.75);
begin;
insert into testnumeric values (3, 3.456);
commit;

-- path
insert into testpath values (23, '(1, 2), (20, -10)');
update testpath set b = '(-1, -1)' where a = 23;
delete from testpath where a = 23;
begin;
insert into testpath values (34, '(0, 0), (3, 4), (5, 5), (1, 2)');
commit;

-- point
insert into testpoint values (11, '(0, -1)');
update testpoint set b = '(1, 3)' where a = 11;
delete from testpoint where a = 11;
begin;
insert into testpoint values (21, '(33, 44)');
commit;

-- polygon
insert into testpolygon values (1, '(1, 3), (4, 12), (2, 4)');
update testpolygon set b = '(1, 3), (4, 12), (2, 4), (1, 4)' where a = 1;
delete from testpolygon where a = 1;
begin;
insert into testpolygon values (27, '(1, 3), (2, 4), (1, 4)');
commit;

-- text
insert into testtext values (1, 'sample string with pk 1');
insert into testtext values (3, 'sample string with pk 3');
begin;
update testtext set a = a + 1, b = 'sample string with pk 2' where a = 1;
commit;
update testtext set b = 'random sample string' where a = 3;

-- time
insert into testtime values (1, '11:30:59');
update testtime set b = '23:30:59' where a = 1;
begin;
insert into testtime values (2, '00:00:01');
update testtime set b = b + '00:00:59' where a = 2;
commit;
delete from testtime where a = 1;
delete from testtime where a = 2;

-- timestamp
insert into testtimestamp values (1, '2017-07-04 12:30:30');
insert into testtimestamp values (2, '2021-09-29 00:00:00');
update testtimestamp set b = '1970-01-01 00:00:10' where a = 1;

-- timetz
insert into testtimetz values (1, '11:30:59+05:30');
update testtimetz set b = '23:30:59+05:30' where a = 1;
begin;
insert into testtimetz values (2, '00:00:01 UTC');
commit;
delete from testtimetz where a = 1;
delete from testtimetz where a = 2;

-- uuid
insert into testuuid values (1, 'ffffffff-ffff-ffff-ffff-ffffffffffff');
insert into testuuid values (3, 'ffffffff-ffff-ffff-ffff-ffffffffffff');
begin;
update testuuid set b = '123e4567-e89b-12d3-a456-426655440000' where a = 3;
commit;
delete from testuuid where a = 1;
insert into testuuid values (2, '123e4567-e89b-12d3-a456-426655440000');

-- varbit
insert into testvarbit values (1, '001111');
insert into testvarbit values (2, '1101011101');
begin;
insert into testvarbit values (3, '11');
update testvarbit set b = '0', a = 0 where a = 1;
commit;
delete from testvarbit where a = 2;

-- timestamptz
insert into testtstz values (1, '1970-01-01 00:10:00+05:30');
begin;
update testtstz set b = '2022-01-01 00:10:00+05:30' where a = 1;
delete from testtstz where a = 1;
commit;

-- int4range
insert into testint4range values (1, '(4, 14)');
begin;
update testint4range set b = '(5, 43)' where a = 1;
delete from testint4range where a = 1;
commit;

-- int8range
insert into testint8range values (1, '(4, 15)');
begin;
update testint8range set b = '(1, 100000)' where a = 1;
delete from testint8range where a = 1;
commit;

-- tsrange
insert into testtsrange values (1, '(1970-01-01 00:00:00, 2000-01-01 12:00:00)');
begin;
update testtsrange set b = '(1970-01-01 00:00:00, 2022-11-01 12:00:00)' where a = 1;
delete from testtsrange where a = 1;
commit;

-- tstzrange
insert into testtstzrange values (1, '(2017-07-04 12:30:30 UTC, 2021-07-04 12:30:30+05:30)');
begin;
update testtstzrange set b = '(2017-07-04 12:30:30 UTC, 2021-10-04 12:30:30+05:30)' where a = 1;
delete from testtstzrange where a = 1;
commit;

-- daterange
insert into testdaterange values (1, '(2019-10-07, 2021-10-07)');
begin;
update testdaterange set b = '(2019-10-07, 2020-10-07)' where a = 1;
delete from testdaterange where a = 1;
commit;

-- UDT
insert into testdiscount values (1, 'FIXED');