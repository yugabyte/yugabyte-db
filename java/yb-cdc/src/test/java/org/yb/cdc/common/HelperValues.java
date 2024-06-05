package org.yb.cdc.common;

public class HelperValues {
  public static final String dropAllTablesWithTypes = "drop table if exists testbit, " +
    "testboolean, testbox, testbytea, testcidr, testcircle, testdate, testdouble, testinet, " +
    "testint, testjson, testjsonb, testline, testlseg, testmacaddr8, testmacaddr, " +
    "testmoney, testnumeric, testpath, testpoint, testpolygon, testtext, testtime, " +
    "testtimestamp, testtimetz, testuuid, testvarbit, testtstz, testint4range, " +
    "testint8range, testtsrange, testtstzrange, testdaterange;";

  public static final String createTableWithDefaults = "create table testdefault " +
    "(a int primary key, " +
    "bitval bit(4) default '1111', boolval boolean default TRUE, " +
    "boxval box default '(0,0),(1,1)', byteval bytea default E'\\\\001', " +
    "cidrval cidr default '10.1.0.0/16', crcl circle default '0,0,5'," +
    "dt date default '2000-01-01', dp double precision default 32.34, " +
    "inetval inet default '127.0.0.1', i int default 404, " +
    "js json default '{\"a\":\"b\"}', jsb jsonb default '{\"a\":\"b\"}', " +
    "ln line default '{1,2,-8}', ls lseg default '[(0,0),(2,4)]', " +
    "mc8 macaddr8 default '22:00:5c:03:55:08:01:02', mc macaddr default '2C:54:91:88:C9:E3', " +
    "mn money default 100, nm numeric default 12.34, " +
    "pth path default '(1,2),(20,-10)', pnt point default '(0,0)', " +
    "poly polygon default '(1,3),(4,12),(2,4)', txt text default 'default text value', " +
    "tm time default '00:00:00', ts timestamp default '2000-09-01 00:00:00', " +
    "ttz timetz default '00:00:00+05:30', " +
    "u uuid default 'ffffffff-ffff-ffff-ffff-ffffffffffff', " +
    "vb varbit(4) default '11', tstz timestamptz default '1970-01-01 00:10:00+05:30', " +
    "i4r int4range default '(1,10)', i8r int8range default '(100, 200)', " +
    "tsr tsrange default '(1970-01-01 00:00:00, 1970-01-01 12:00:00)', " +
    "tstzr tstzrange default '(2017-07-04 12:30:30 UTC, 2021-07-04 12:30:30+05:30)', " +
    "dr daterange default '(1970-01-01,2000-01-01)');";

  public static Object[] expectedDefaultValues = {"1111", true, "(1,1),(0,0)", "\\x01",
    "10.1.0.0/16", "<(0,0),5>", "2000-01-01", 32.34, "127.0.0.1", 404, "{\"a\":\"b\"}",
    "{\"a\": \"b\"}", "{1,2,-8}", "[(0,0),(2,4)]", "22:00:5c:03:55:08:01:02",
    "2c:54:91:88:c9:e3", "$100.00", "12.34", "((1,2),(20,-10))", "(0,0)", "((1,3),(4,12),(2,4))",
    "default text value", "00:00:00", "2000-09-01 00:00:00", "00:00:00+05:30",
    "ffffffff-ffff-ffff-ffff-ffffffffffff", "11", "1969-12-31 18:40:00+00", "[2,10)",
    "[101,200)", "(\"1970-01-01 00:00:00\",\"1970-01-01 12:00:00\")",
    "(\"2017-07-04 12:30:30+00\",\"2021-07-04 07:00:30+00\")", "[1970-01-02,2000-01-01)",
    ""};

  public static String insertionTemplateForArrays = "insert into %s values (1, %s, %s, %s, %s, " +
    "%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, " +
    "%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s);";

  public static String dropAllArrayTables = "drop table if exists testvb, " +
    "testboolval, testchval, testvchar, testdt, testdp, testinetval, testintval, testjsonval, " +
    "testjsonbval, testmac, testmac8, testmoneyval, testrl, testsi, testtextval, " +
    "testtval, testttzval, testtimestampval, testtimestamptzval, testu, testi4r, testi8r, " +
    "testdr, testtsr, testtstzr, testnr, testbx, testln, testls, testpt, testcrcl, " +
    "testpoly, testpth, testinterv, testcidrval, testtxid";

  public static String createTableWithMultiDimensionalArrayColumns = "create table testmulti " +
    "(a int primary key, vb varbit(10)[], boolval boolean[], chval char(5)[], " +
    "vchar varchar(20)[], dt date[], " + "dp double precision[], " +
    "inetval inet[], intval integer[], jsonval json[], jsonbval jsonb[], mac macaddr[], " +
    "mac8 macaddr8[], moneyval money[], rl real[], si smallint[], textval text[], " +
    "tval time[], ttzval timetz[], timestampval timestamp[], timestamptzcal timestamptz[], " +
    "u uuid[], i4r int4range[], i8r int8range[], dr daterange[], tsr tsrange[], " +
    "tstzr tstzrange[], nr numrange[], bx box[], ln line[], ls lseg[], pt point[], " +
    "crcl circle[], poly polygon[], pth path[], interv interval[], cidrval cidr[], " +
    "txid txid_snapshot[]);";

  public static String createTableWithSingleDimensionalArrayColumns = "create table testsingle " +
    "(a int primary key, vb varbit(10)[], boolval boolean[], chval char(5)[], " +
    "vchar varchar(20)[], dt date[], " + "dp double precision[], " +
    "inetval inet[], intval integer[], jsonval json[], jsonbval jsonb[], mac macaddr[], " +
    "mac8 macaddr8[], moneyval money[], rl real[], si smallint[], textval text[], " +
    "tval time[], ttzval timetz[], timestampval timestamp[], timestamptzcal timestamptz[], " +
    "u uuid[], i4r int4range[], i8r int8range[], dr daterange[], tsr tsrange[], " +
    "tstzr tstzrange[], nr numrange[], bx box[], ln line[], ls lseg[], pt point[], " +
    "crcl circle[], poly polygon[], pth path[], interv interval[], cidrval cidr[], " +
    "txid txid_snapshot[]);";

  public static String[] expectedMultiDimensionalArrayColumnRecords = {
    "{{1011,011101,1101110111},{1011,011101,1101110111}}",
    "{{f,t,t,f},{f,t,t,f}}",
    "{{five5,five5},{five5,five5}}",
    "{{\"sample varchar\",\"test string\"},{\"sample varchar\",\"test string\"}}",
    "{{2021-10-07,1970-01-01},{2021-10-07,1970-01-01}}",
    "{{1.23,2.34,3.45},{1.23,2.34,3.45}}",
    "{{127.0.0.1,192.168.1.1},{127.0.0.1,192.168.1.1}}",
    "{{1,2,3},{1,2,3}}",
    "{{\"{\\\"a\\\":\\\"b\\\"}\",\"{\\\"c\\\":\\\"d\\\"}\"}," +
      "{\"{\\\"a\\\":\\\"b\\\"}\",\"{\\\"c\\\":\\\"d\\\"}\"}}",
    "{{\"{\\\"a\\\": \\\"b\\\"}\",\"{\\\"c\\\": \\\"d\\\"}\"}," +
      "{\"{\\\"a\\\": \\\"b\\\"}\",\"{\\\"c\\\": \\\"d\\\"}\"}}",
    "{{2c:54:91:88:c9:e3,2c:b8:01:76:c9:e3,2c:54:f1:88:c9:e3}," +
      "{2c:54:91:88:c9:e3,2c:b8:01:76:c9:e3,2c:54:f1:88:c9:e3}}",
    "{{22:00:5c:03:55:08:01:02,22:10:5c:03:55:d8:f1:02}," +
      "{22:00:5c:03:55:08:01:02,22:10:5c:03:55:d8:f1:02}}",
    "{{$100.55,$200.50,$50.05},{$100.55,$200.50,$50.05}}",
    "{{1.23,4.56,7.8901},{1.23,4.56,7.8901}}",
    "{{1,2,3,4,5,6},{1,2,3,4,5,6}}",
    "{{sample1,sample2},{sample1,sample2}}",
    "{{12:00:32,22:10:20,23:59:59,00:00:00},{12:00:32,22:10:20,23:59:59,00:00:00}}",
    "{{11:00:00+05:30,23:00:59+00,09:59:00+00},{11:00:00+05:30,23:00:59+00,09:59:00+00}}",
    "{{\"1970-01-01 00:00:10\",\"2000-01-01 00:00:10\"}," +
      "{\"1970-01-01 00:00:10\",\"2000-01-01 00:00:10\"}}",
    "{{\"1969-12-31 18:30:10+00\",\"2000-01-01 00:00:10+00\"}," +
      "{\"1969-12-31 18:30:10+00\",\"2000-01-01 00:00:10+00\"}}",
    "{{123e4567-e89b-12d3-a456-426655440000,123e4567-e89b-12d3-a456-426655440000}," +
      "{123e4567-e89b-12d3-a456-426655440000,123e4567-e89b-12d3-a456-426655440000}}",
    "{{\"[2,5)\",\"[11,100)\"},{\"[2,5)\",\"[11,100)\"}}",
    "{{\"[2,10)\",\"[901,10000)\"},{\"[2,10)\",\"[901,10000)\"}}",
    "{{\"[2000-09-21,2021-10-08)\",\"[1970-01-02,2000-01-01)\"}," +
      "{\"[2000-09-21,2021-10-08)\",\"[1970-01-02,2000-01-01)\"}}",
    "{{\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"," +
      "\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"}," +
      "{\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"," +
      "\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"}}",
    "{{\"(\\\"2017-07-04 12:30:30+00\\\",\\\"2021-07-04 07:00:30+00\\\")\"," +
      "\"(\\\"1970-09-14 12:30:30+00\\\",\\\"2021-10-13 04:02:30+00\\\")\"}," +
      "{\"(\\\"2017-07-04 12:30:30+00\\\",\\\"2021-07-04 07:00:30+00\\\")\"," +
      "\"(\\\"1970-09-14 12:30:30+00\\\",\\\"2021-10-13 04:02:30+00\\\")\"}}",
    "{{\"(10.42,11.354)\",\"(-0.99,100.9)\"},{\"(10.42,11.354)\",\"(-0.99,100.9)\"}}",
    "{{(8,9),(1,3);(9,27),(-1,-1)};{(8,9),(1,3);(9,27),(-1,-1)}}",
    "{{\"{2.5,-1,0}\",\"{1,2,-10}\"},{\"{2.5,-1,0}\",\"{1,2,-10}\"}}",
    "{{\"[(0,0),(2,5)]\",\"[(0,5),(6,2)]\"},{\"[(0,0),(2,5)]\",\"[(0,5),(6,2)]\"}}",
    "{{\"(1,2)\",\"(10,11.5)\",\"(0,-1)\"},{\"(1,2)\",\"(10,11.5)\",\"(0,-1)\"}}",
    "{{\"<(1,2),4>\",\"<(-1,0),5>\"},{\"<(1,2),4>\",\"<(-1,0),5>\"}}",
    "{{\"((1,3),(4,12),(2,4))\",\"((1,-1),(4,-12),(-2,-4))\"}," +
      "{\"((1,3),(4,12),(2,4))\",\"((1,-1),(4,-12),(-2,-4))\"}}",
    "{{\"((1,2),(10,15),(0,0))\",\"((1,2),(10,15),(10,0),(-3,-2))\"}," +
      "{\"((1,2),(10,15),(0,0))\",\"((1,2),(10,15),(10,0),(-3,-2))\"}}",
    "{{01:16:06.2,\"29 days\"},{01:16:06.2,\"29 days\"}}",
    "{{12.2.0.0/22,10.1.0.0/16},{12.2.0.0/22,10.1.0.0/16}}",
    "{{3:3:,3:3:},{3:3:,3:3:}}"};

  public static String[] expectedSingleDimensionalArrayColumnRecords = {
    "{1011,011101,1101110111}",
    "{f,t,t,f}",
    "{five5,five5}",
    "{\"sample varchar\",\"test string\"}",
    "{2021-10-07,1970-01-01}",
    "{1.23,2.34,3.45}",
    "{127.0.0.1,192.168.1.1}",
    "{1,2,3}",
    "{\"{\\\"a\\\":\\\"b\\\"}\",\"{\\\"c\\\":\\\"d\\\"}\"}",
    "{\"{\\\"a\\\": \\\"b\\\"}\",\"{\\\"c\\\": \\\"d\\\"}\"}",
    "{2c:54:91:88:c9:e3,2c:b8:01:76:c9:e3,2c:54:f1:88:c9:e3}",
    "{22:00:5c:03:55:08:01:02,22:10:5c:03:55:d8:f1:02}",
    "{$100.55,$200.50,$50.05}",
    "{1.23,4.56,7.8901}",
    "{1,2,3,4,5,6}",
    "{sample1,sample2}",
    "{12:00:32,22:10:20,23:59:59,00:00:00}",
    "{11:00:00+05:30,23:00:59+00,09:59:00+00}",
    "{\"1970-01-01 00:00:10\",\"2000-01-01 00:00:10\"}",
    "{\"1969-12-31 18:30:10+00\",\"2000-01-01 00:00:10+00\"}",
    "{123e4567-e89b-12d3-a456-426655440000,123e4567-e89b-12d3-a456-426655440000}",
    "{\"[2,5)\",\"[11,100)\"}",
    "{\"[2,10)\",\"[901,10000)\"}",
    "{\"[2000-09-21,2021-10-08)\",\"[1970-01-02,2000-01-01)\"}",
    "{\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"," +
      "\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"}",
    "{\"(\\\"2017-07-04 12:30:30+00\\\",\\\"2021-07-04 07:00:30+00\\\")\"," +
      "\"(\\\"1970-09-14 12:30:30+00\\\",\\\"2021-10-13 04:02:30+00\\\")\"}",
    "{\"(10.42,11.354)\",\"(-0.99,100.9)\"}",
    "{(8,9),(1,3);(9,27),(-1,-1)}",
    "{\"{2.5,-1,0}\",\"{1,2,-10}\"}",
    "{\"[(0,0),(2,5)]\",\"[(0,5),(6,2)]\"}",
    "{\"(1,2)\",\"(10,11.5)\",\"(0,-1)\"}",
    "{\"<(1,2),4>\",\"<(-1,0),5>\"}",
    "{\"((1,3),(4,12),(2,4))\",\"((1,-1),(4,-12),(-2,-4))\"}",
    "{\"((1,2),(10,15),(0,0))\",\"((1,2),(10,15),(10,0),(-3,-2))\"}",
    "{01:16:06.2,\"29 days\"}",
    "{12.2.0.0/22,10.1.0.0/16}",
    "{3:3:,3:3:}"};
}
