insert into testvb values (1, '{1011, 011101, 1101110111}');

insert into testboolval values (1, '{FALSE, TRUE, TRUE, FALSE}');

insert into testchval values (1, '{"five5", "five5"}');

insert into testvchar values (1, '{"sample varchar", "test string"}');

insert into testdt values (1, '{"2021-10-07", "1970-01-01"}');

insert into testdp values (1, '{1.23, 2.34, 3.45}');

insert into testinetval values (1, '{127.0.0.1, 192.168.1.1}');

insert into testintval values (1, '{1, 2, 3}');

insert into testjsonval values (1, array['{"a":"b"}', '{"c":"d"}']::json[]);

insert into testjsonbval values (1, array['{"a":"b"}', '{"c":"d"}']::jsonb[]);

insert into testmac values (1, '{2c:54:91:88:c9:e3, 2c:b8:01:76:c9:e3, 2c:54:f1:88:c9:e3}');

insert into testmac8 values (1, '{22:00:5c:03:55:08:01:02, 22:10:5c:03:55:d8:f1:02}');

insert into testmoneyval values (1, '{100.55, 200.50, 50.05}');

insert into testrl values (1, '{1.23, 4.56, 7.8901}');

insert into testsi values (1, '{1, 2, 3, 4, 5, 6}');

insert into testtextval values (1, '{"sample1", "sample2"}');

insert into testtval values (1, '{12:00:32, 22:10:20, 23:59:59, 00:00:00}');

insert into testttzval values (1, '{11:00:00+05:30, 23:00:59+00, 09:59:00 UTC}');

insert into testtimestampval values (1, '{1970-01-01 0:00:10, 2000-01-01 0:00:10}');

insert into testtimestamptzval values (1, '{1970-01-01 0:00:10+05:30, 2000-01-01 0:00:10 UTC}');

insert into testu values (1, '{123e4567-e89b-12d3-a456-426655440000, 123e4567-e89b-12d3-a456-426655440000}');

insert into testi4r values (1, array['(1, 5)', '(10, 100)']::int4range[]);

insert into testi8r values (1, array['(1, 10)', '(900, 10000)']::int8range[]);

insert into testdr values (1, array['(2000-09-20, 2021-10-08)', '(1970-01-01, 2000-01-01)']::daterange[]);

insert into testtsr values (1, array['(1970-01-01 00:00:00, 2000-01-01 12:00:00)', '(1970-01-01 00:00:00, 2000-01-01 12:00:00)']::tsrange[]);

insert into testtstzr values (1, array['(2017-07-04 12:30:30 UTC, 2021-07-04 12:30:30+05:30)', '(1970-09-14 12:30:30 UTC, 2021-10-13 09:32:30+05:30)']::tstzrange[]);

insert into testnr values (1, array['(10.42, 11.354)', '(-0.99, 100.9)']::numrange[]);

insert into testbx values (1, array['(8,9), (1,3)', '(-1,-1), (9,27)']::box[]);

insert into testln values (1, array['[(0, 0), (2, 5)]', '{1, 2, -10}']::line[]);

insert into testls values (1, array['[(0, 0), (2, 5)]', '[(0, 5), (6, 2)]']::lseg[]);

insert into testpt values (1, array['(1, 2)', '(10, 11.5)', '(0, -1)']::point[]);

insert into testcrcl values (1, array['1, 2, 4', '-1, 0, 5']::circle[]);

insert into testpoly values (1, array['(1, 3), (4, 12), (2, 4)', '(1, -1), (4, -12), (-2, -4)']::polygon[]);

insert into testpth values (1, array['(1, 2), (10, 15), (0, 0)', '(1, 2), (10, 15), (10, 0), (-3, -2)']::path[]);

insert into testinterv values (1, array['2020-03-10 13:47:19.7':: timestamp - '2020-03-10 12:31:13.5':: timestamp, '2020-03-10 00:00:00':: timestamp - '2020-02-10 00:00:00':: timestamp]::interval[]);

insert into testcidrval values (1, array['12.2.0.0/22', '10.1.0.0/16']::cidr[]);

insert into testtxid values (1, array[txid_current_snapshot(), txid_current_snapshot()]::txid_snapshot[]);
