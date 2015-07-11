/* contrib/orafce/orafce--unpackaged--3.1.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION orafce FROM unpackaged" to load this file. \quit

ALTER EXTENSION orafce ADD function pg_catalog.trunc(value date, fmt text);
ALTER EXTENSION orafce ADD function pg_catalog.round(value date, fmt text);
ALTER EXTENSION orafce ADD function pg_catalog.next_day(value date, weekday text);
ALTER EXTENSION orafce ADD function pg_catalog.next_day(value date, weekday integer);
ALTER EXTENSION orafce ADD function pg_catalog.last_day(value date);
ALTER EXTENSION orafce ADD function pg_catalog.months_between(date1 date, date2 date);
ALTER EXTENSION orafce ADD function pg_catalog.add_months(day date, value int);
ALTER EXTENSION orafce ADD function pg_catalog.trunc(value timestamp with time zone, fmt text);
ALTER EXTENSION orafce ADD function pg_catalog.round(value timestamp with time zone, fmt text);
ALTER EXTENSION orafce ADD function pg_catalog.round(value timestamp with time zone);
ALTER EXTENSION orafce ADD function pg_catalog.round(value date);
ALTER EXTENSION orafce ADD function pg_catalog.trunc(value timestamp with time zone);
ALTER EXTENSION orafce ADD function pg_catalog.trunc(value date);
ALTER EXTENSION orafce ADD function pg_catalog.nlssort(text, text);
ALTER EXTENSION orafce ADD function pg_catalog.nlssort(text);
ALTER EXTENSION orafce ADD function pg_catalog.set_nls_sort(text);
ALTER EXTENSION orafce ADD function pg_catalog.instr(str text, patt text, start int, nth int);
ALTER EXTENSION orafce ADD function pg_catalog.instr(str text, patt text, start int);
ALTER EXTENSION orafce ADD function pg_catalog.instr(str text, patt text);
ALTER EXTENSION orafce ADD function pg_catalog.to_char(num smallint);
ALTER EXTENSION orafce ADD function pg_catalog.to_char(num int);
ALTER EXTENSION orafce ADD function pg_catalog.to_char(num bigint);
ALTER EXTENSION orafce ADD function pg_catalog.to_char(num real);
ALTER EXTENSION orafce ADD function pg_catalog.to_char(num double precision);
ALTER EXTENSION orafce ADD function pg_catalog.to_char(num numeric);
ALTER EXTENSION orafce ADD function pg_catalog.to_number(str text);
ALTER EXTENSION orafce ADD function pg_catalog.to_number(numeric);
ALTER EXTENSION orafce ADD function pg_catalog.to_number(numeric,numeric)
ALTER EXTENSION orafce ADD function pg_catalog.to_date(str text);
ALTER EXTENSION orafce ADD function to_multi_byte(str text);
ALTER EXTENSION orafce ADD function bitand(bigint, bigint);
ALTER EXTENSION orafce ADD function sinh(float8);
ALTER EXTENSION orafce ADD function cosh(float8);
ALTER EXTENSION orafce ADD function tanh(float8);
ALTER EXTENSION orafce ADD function nanvl(float4, float4);
ALTER EXTENSION orafce ADD function nanvl(float8, float8);
ALTER EXTENSION orafce ADD function nanvl(numeric, numeric);
ALTER EXTENSION orafce ADD function dump("any");
ALTER EXTENSION orafce ADD function dump("any", integer);

ALTER EXTENSION orafce ADD schema plvstr;
ALTER EXTENSION orafce ADD function plvstr.rvrs(str text, start int, _end int);
ALTER EXTENSION orafce ADD function plvstr.rvrs(str text, start int);
ALTER EXTENSION orafce ADD function plvstr.rvrs(str text);
ALTER EXTENSION orafce ADD function pg_catalog.lnnvl(bool);

-- can't overwrite PostgreSQL functions!!!!

ALTER EXTENSION orafce ADD schema oracle;
ALTER EXTENSION orafce ADD function oracle.substr(str text, start int);
ALTER EXTENSION orafce ADD function oracle.substr(str text, start int, len int);
ALTER EXTENSION orafce ADD function oracle.substr(numeric,numeric);
ALTER EXTENSION orafce ADD function oracle.substr(numeric, numeric, numeric);
ALTER EXTENSION orafce ADD function oracle.substr(varchar, numeric);
ALTER EXTENSION orafce ADD function oracle.substr(varchar, numeric,numeric);
ALTER EXTENSION orafce ADD function oracle.to_date(text);
ALTER EXTENSION orafce ADD function oracle.to_date(TEXT,TEXT);
ALTER EXTENSION orafce ADD function oracle.add_days_to_timestamp(oracle.date,integer);
ALTER EXTENSION orafce ADD function oracle.add_days_to_timestamp(oracle.date,smallint);
ALTER EXTENSION orafce ADD function oracle.add_days_to_timestamp(oracle.date,bigint);
ALTER EXTENSION orafce ADD function oracle.add_days_to_timestamp(oracle.date,numeric);
ALTER EXTENSION orafce ADD function oracle.subtract (oracle.date, integer);
ALTER EXTENSION orafce ADD function oracle.subtract (oracle.date, smallint);
ALTER EXTENSION orafce ADD function oracle.subtract (oracle.date, bigint);
ALTER EXTENSION orafce ADD function oracle.subtract (oracle.date, numeric);
ALTER EXTENSION orafce ADD function oracle.subtract (oracle.date, oracle.date);
ALTER EXTENSION orafce ADD function oracle.add_months(TIMESTAMP WITH TIME ZONE,INTEGER);
ALTER EXTENSION orafce ADD function oracle.last_day(TIMESTAMPTZ);
ALTER EXTENSION orafce ADD function oracle.months_between(TIMESTAMP WITH TIME ZONE,TIMESTAMP WITH TIME ZONE);
ALTER EXTENSION orafce ADD function oracle.next_day(TIMESTAMP WITH TIME ZONE,INTEGER);
ALTER EXTENSION orafce ADD function oracle.next_day(TIMESTAMP WITH TIME ZONE,TEXT);

-- emulation of dual table
ALTER EXTENSION orafce ADD view public.dual;

-- this packege is emulation of dbms_output Oracle packege
--

ALTER EXTENSION orafce ADD schema dbms_output;
ALTER EXTENSION orafce ADD function dbms_output.enable(IN buffer_size int4);
ALTER EXTENSION orafce ADD function dbms_output.enable();
ALTER EXTENSION orafce ADD function dbms_output.disable();
ALTER EXTENSION orafce ADD function dbms_output.serveroutput(IN bool);
ALTER EXTENSION orafce ADD function dbms_output.put(IN a text);
ALTER EXTENSION orafce ADD function dbms_output.put_line(IN a text);
ALTER EXTENSION orafce ADD function dbms_output.new_line();
ALTER EXTENSION orafce ADD function dbms_output.get_line(OUT line text, OUT status int4);
ALTER EXTENSION orafce ADD function dbms_output.get_lines(OUT lines text[], INOUT numlines int4);


-- others functions

ALTER EXTENSION orafce ADD function nvl(anyelement, anyelement);
ALTER EXTENSION orafce ADD function nvl2(anyelement, anyelement, anyelement);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, text);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, text, text);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, text, anyelement, text);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, text, anyelement, text, text);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, text, anyelement, text, anyelement, text);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, text, anyelement, text, anyelement, text, text);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bpchar);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bpchar, bpchar);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bpchar, anyelement, bpchar);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bpchar, anyelement, bpchar, bpchar);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bpchar, anyelement, bpchar, anyelement, bpchar);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bpchar, anyelement, bpchar, anyelement, bpchar, bpchar);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, integer);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, integer, integer);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, integer, anyelement, integer);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, integer, anyelement, integer, integer);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, integer, anyelement, integer, anyelement, integer);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, integer, anyelement, integer, anyelement, integer, integer);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bigint);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bigint, bigint);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bigint, anyelement, bigint);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bigint, anyelement, bigint, bigint);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bigint, anyelement, bigint, anyelement, bigint);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, bigint, anyelement, bigint, anyelement, bigint, bigint);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, numeric);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, numeric, numeric);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, numeric, anyelement, numeric);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, numeric, anyelement, numeric, numeric);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, numeric, anyelement, numeric, anyelement, numeric);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, numeric, anyelement, numeric, anyelement, numeric, numeric);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, date);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, date, date);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, date, anyelement, date);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, date, anyelement, date, date);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, date, anyelement, date, anyelement, date);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, date, anyelement, date, anyelement, date, date);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, time);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, time, time);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, time, anyelement, time);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, time, anyelement, time, time);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, time, anyelement, time, anyelement, time);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, time, anyelement, time, anyelement, time, time);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamp);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamp, timestamp);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamp, anyelement, timestamp);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamp, anyelement, timestamp, timestamp);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamp, anyelement, timestamp, anyelement, timestamp);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamp, anyelement, timestamp, anyelement, timestamp, timestamp);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamptz);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamptz, timestamptz);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamptz, anyelement, timestamptz);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamptz, anyelement, timestamptz, timestamptz);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamptz, anyelement, timestamptz, anyelement, timestamptz);
ALTER EXTENSION orafce ADD function decode(anyelement, anyelement, timestamptz, anyelement, timestamptz, anyelement, timestamptz, timestamptz);

ALTER EXTENSION orafce ADD schema dbms_pipe;
ALTER EXTENSION orafce ADD function dbms_pipe.pack_message(text);
ALTER EXTENSION orafce ADD function dbms_pipe.unpack_message_text();
ALTER EXTENSION orafce ADD function dbms_pipe.receive_message(text, int);
ALTER EXTENSION orafce ADD function dbms_pipe.receive_message(text);
ALTER EXTENSION orafce ADD function dbms_pipe.send_message(text, int, int);
ALTER EXTENSION orafce ADD function dbms_pipe.send_message(text, int);
ALTER EXTENSION orafce ADD function dbms_pipe.send_message(text);
ALTER EXTENSION orafce ADD function dbms_pipe.unique_session_name();
ALTER EXTENSION orafce ADD function dbms_pipe.__list_pipes();

ALTER EXTENSION orafce ADD VIEW dbms_pipe.db_pipes;
ALTER EXTENSION orafce ADD function dbms_pipe.next_item_type();
ALTER EXTENSION orafce ADD function dbms_pipe.create_pipe(text, int, bool);
ALTER EXTENSION orafce ADD function dbms_pipe.create_pipe(text, int);
ALTER EXTENSION orafce ADD function dbms_pipe.create_pipe(text);
ALTER EXTENSION orafce ADD function dbms_pipe.reset_buffer();
ALTER EXTENSION orafce ADD function dbms_pipe.purge(text);
ALTER EXTENSION orafce ADD function dbms_pipe.remove_pipe(text);
ALTER EXTENSION orafce ADD function dbms_pipe.pack_message(date);
ALTER EXTENSION orafce ADD function dbms_pipe.unpack_message_date();
ALTER EXTENSION orafce ADD function dbms_pipe.pack_message(timestamp with time zone);
ALTER EXTENSION orafce ADD function dbms_pipe.unpack_message_timestamp();
ALTER EXTENSION orafce ADD function dbms_pipe.pack_message(numeric);
ALTER EXTENSION orafce ADD function dbms_pipe.unpack_message_number();
ALTER EXTENSION orafce ADD function dbms_pipe.pack_message(integer);
ALTER EXTENSION orafce ADD function dbms_pipe.pack_message(bigint);
ALTER EXTENSION orafce ADD function dbms_pipe.pack_message(bytea);
ALTER EXTENSION orafce ADD function dbms_pipe.unpack_message_bytea();
ALTER EXTENSION orafce ADD function dbms_pipe.pack_message(record);
ALTER EXTENSION orafce ADD function dbms_pipe.unpack_message_record();

-- follow package PLVdate emulation

ALTER EXTENSION orafce ADD schema plvdate;
ALTER EXTENSION orafce ADD function plvdate.add_bizdays(date, int);
ALTER EXTENSION orafce ADD function plvdate.nearest_bizday(date);
ALTER EXTENSION orafce ADD function plvdate.next_bizday(date);
ALTER EXTENSION orafce ADD function plvdate.bizdays_between(date, date);
ALTER EXTENSION orafce ADD function plvdate.prev_bizday(date);
ALTER EXTENSION orafce ADD function plvdate.isbizday(date);
ALTER EXTENSION orafce ADD function plvdate.set_nonbizday(text);
ALTER EXTENSION orafce ADD function plvdate.unset_nonbizday(text);
ALTER EXTENSION orafce ADD function plvdate.set_nonbizday(date, bool);
ALTER EXTENSION orafce ADD function plvdate.unset_nonbizday(date, bool);
ALTER EXTENSION orafce ADD function plvdate.set_nonbizday(date);
ALTER EXTENSION orafce ADD function plvdate.unset_nonbizday(date);
ALTER EXTENSION orafce ADD function plvdate.use_easter(bool);
ALTER EXTENSION orafce ADD function plvdate.use_easter();
ALTER EXTENSION orafce ADD function plvdate.unuse_easter();
ALTER EXTENSION orafce ADD function plvdate.using_easter();
ALTER EXTENSION orafce ADD function plvdate.include_start(bool);
ALTER EXTENSION orafce ADD function plvdate.include_start();
ALTER EXTENSION orafce ADD function plvdate.noinclude_start();
ALTER EXTENSION orafce ADD function plvdate.including_start();
ALTER EXTENSION orafce ADD function plvdate.version();
ALTER EXTENSION orafce ADD function plvdate.default_holidays(text);
ALTER EXTENSION orafce ADD function plvdate.days_inmonth(date);
ALTER EXTENSION orafce ADD function plvdate.isleapyear(date);


-- PLVstr package
ALTER EXTENSION orafce ADD schema plvstr;
ALTER EXTENSION orafce ADD function plvstr.normalize(str text);
ALTER EXTENSION orafce ADD function plvstr.is_prefix(str text, prefix text, cs bool);
ALTER EXTENSION orafce ADD function plvstr.is_prefix(str text, prefix text);
ALTER EXTENSION orafce ADD function plvstr.is_prefix(str int, prefix int);
ALTER EXTENSION orafce ADD function plvstr.is_prefix(str bigint, prefix bigint);
ALTER EXTENSION orafce ADD function plvstr.substr(str text, start int, len int);
ALTER EXTENSION orafce ADD function plvstr.substr(str text, start int);
ALTER EXTENSION orafce ADD function plvstr.instr(str text, patt text, start int, nth int);
ALTER EXTENSION orafce ADD function plvstr.instr(str text, patt text, start int);
ALTER EXTENSION orafce ADD function plvstr.instr(str text, patt text);
ALTER EXTENSION orafce ADD function plvstr.lpart(str text, div text, start int, nth int, all_if_notfound bool);
ALTER EXTENSION orafce ADD function plvstr.lpart(str text, div text, start int, nth int);
ALTER EXTENSION orafce ADD function plvstr.lpart(str text, div text, start int);
ALTER EXTENSION orafce ADD function plvstr.lpart(str text, div text);
ALTER EXTENSION orafce ADD function plvstr.rpart(str text, div text, start int, nth int, all_if_notfound bool);
ALTER EXTENSION orafce ADD function plvstr.rpart(str text, div text, start int, nth int);
ALTER EXTENSION orafce ADD function plvstr.rpart(str text, div text, start int);
ALTER EXTENSION orafce ADD function plvstr.rpart(str text, div text);
ALTER EXTENSION orafce ADD function plvstr.lstrip(str text, substr text, num int);
ALTER EXTENSION orafce ADD function plvstr.lstrip(str text, substr text);
ALTER EXTENSION orafce ADD function plvstr.rstrip(str text, substr text, num int);
ALTER EXTENSION orafce ADD function plvstr.rstrip(str text, substr text);
ALTER EXTENSION orafce ADD function plvstr.swap(str text, replace text, start int, length int);
ALTER EXTENSION orafce ADD function plvstr.swap(str text, replace text);
ALTER EXTENSION orafce ADD function plvstr.betwn(str text, start int, _end int, inclusive bool);
ALTER EXTENSION orafce ADD function plvstr.betwn(str text, start int, _end int);
ALTER EXTENSION orafce ADD function plvstr.betwn(str text, start text, _end text, startnth int, endnth int, inclusive bool, gotoend bool);
ALTER EXTENSION orafce ADD function plvstr.betwn(str text, start text, _end text);
ALTER EXTENSION orafce ADD function plvstr.betwn(str text, start text, _end text, startnth int, endnth int);

ALTER EXTENSION orafce ADD schema plvchr;
ALTER EXTENSION orafce ADD function plvchr.nth(str text, n int);
ALTER EXTENSION orafce ADD function plvchr.first(str text);
ALTER EXTENSION orafce ADD function plvchr.last(str text);
ALTER EXTENSION orafce ADD function plvchr._is_kind(str text, kind int);
ALTER EXTENSION orafce ADD function plvchr._is_kind(c int, kind int);
ALTER EXTENSION orafce ADD function plvchr.is_blank(c int);
ALTER EXTENSION orafce ADD function plvchr.is_blank(c text);
ALTER EXTENSION orafce ADD function plvchr.is_digit(c int);
ALTER EXTENSION orafce ADD function plvchr.is_digit(c text);
ALTER EXTENSION orafce ADD function plvchr.is_quote(c int);
ALTER EXTENSION orafce ADD function plvchr.is_quote(c text);
ALTER EXTENSION orafce ADD function plvchr.is_other(c int);
ALTER EXTENSION orafce ADD function plvchr.is_other(c text);
ALTER EXTENSION orafce ADD function plvchr.is_letter(c int);
ALTER EXTENSION orafce ADD function plvchr.is_letter(c text);
ALTER EXTENSION orafce ADD function plvchr.char_name(c text);
ALTER EXTENSION orafce ADD function plvstr.left(str text, n int);
ALTER EXTENSION orafce ADD function plvstr.right(str text, n int);
ALTER EXTENSION orafce ADD function plvchr.quoted1(str text);
ALTER EXTENSION orafce ADD function plvchr.quoted2(str text);
ALTER EXTENSION orafce ADD function plvchr.stripped(str text, char_in text);

-- dbms_alert

ALTER EXTENSION orafce ADD schema dbms_alert;
ALTER EXTENSION orafce ADD function dbms_alert.register(name text);
ALTER EXTENSION orafce ADD function dbms_alert.remove(name text);
ALTER EXTENSION orafce ADD function dbms_alert.removeall();
ALTER EXTENSION orafce ADD function dbms_alert._signal(name text, message text);
ALTER EXTENSION orafce ADD function dbms_alert.waitany(OUT name text, OUT message text, OUT status integer, timeout float8);
ALTER EXTENSION orafce ADD function dbms_alert.waitone(name text, OUT message text, OUT status integer, timeout float8);
ALTER EXTENSION orafce ADD function dbms_alert.set_defaults(sensitivity float8);
ALTER EXTENSION orafce ADD function dbms_alert.defered_signal();
ALTER EXTENSION orafce ADD function dbms_alert.signal(_event text, _message text);

ALTER EXTENSION orafce ADD schema plvsubst;
ALTER EXTENSION orafce ADD function plvsubst.string(template_in text, values_in text[], subst text);
ALTER EXTENSION orafce ADD function plvsubst.string(template_in text, values_in text[]);
ALTER EXTENSION orafce ADD function plvsubst.string(template_in text, vals_in text, delim_in text, subst_in text);
ALTER EXTENSION orafce ADD function plvsubst.string(template_in text, vals_in text);
ALTER EXTENSION orafce ADD function plvsubst.string(template_in text, vals_in text, delim_in text);
ALTER EXTENSION orafce ADD function plvsubst.setsubst(str text);
ALTER EXTENSION orafce ADD function plvsubst.setsubst();
ALTER EXTENSION orafce ADD function plvsubst.subst();

ALTER EXTENSION orafce ADD schema dbms_utility;
ALTER EXTENSION orafce ADD function dbms_utility.format_call_stack(text);
ALTER EXTENSION orafce ADD function dbms_utility.format_call_stack();

ALTER EXTENSION orafce ADD schema plvlex;

ALTER EXTENSION orafce ADD function plvlex.tokens(IN str text, IN skip_spaces bool, IN qualified_names bool,
OUT pos int, OUT token text, OUT code int, OUT class text, OUT separator text, OUT mod text);

ALTER EXTENSION orafce ADD schema utl_file;
ALTER EXTENSION orafce ADD domain utl_file.file_type;

ALTER EXTENSION orafce ADD function utl_file.fopen(location text, filename text, open_mode text, max_linesize integer, encoding name);
ALTER EXTENSION orafce ADD function utl_file.fopen(location text, filename text, open_mode text, max_linesize integer);
ALTER EXTENSION orafce ADD function utl_file.fopen(location text, filename text, open_mode text);
ALTER EXTENSION orafce ADD function utl_file.is_open(file utl_file.file_type);
ALTER EXTENSION orafce ADD function utl_file.get_line(file utl_file.file_type, OUT buffer text);
ALTER EXTENSION orafce ADD function utl_file.get_line(file utl_file.file_type, OUT buffer text, len integer);
ALTER EXTENSION orafce ADD function utl_file.get_nextline(file utl_file.file_type, OUT buffer text);
ALTER EXTENSION orafce ADD function utl_file.put(file utl_file.file_type, buffer text);
ALTER EXTENSION orafce ADD function utl_file.put(file utl_file.file_type, buffer anyelement);
ALTER EXTENSION orafce ADD function utl_file.new_line(file utl_file.file_type);
ALTER EXTENSION orafce ADD function utl_file.new_line(file utl_file.file_type, lines int);
ALTER EXTENSION orafce ADD function utl_file.put_line(file utl_file.file_type, buffer text);
ALTER EXTENSION orafce ADD function utl_file.put_line(file utl_file.file_type, buffer text, autoflush bool);
ALTER EXTENSION orafce ADD function utl_file.putf(file utl_file.file_type, format text, arg1 text, arg2 text, arg3 text, arg4 text, arg5 text);
ALTER EXTENSION orafce ADD function utl_file.putf(file utl_file.file_type, format text, arg1 text, arg2 text, arg3 text, arg4 text);
ALTER EXTENSION orafce ADD function utl_file.putf(file utl_file.file_type, format text, arg1 text, arg2 text, arg3 text);
ALTER EXTENSION orafce ADD function utl_file.putf(file utl_file.file_type, format text, arg1 text, arg2 text);
ALTER EXTENSION orafce ADD function utl_file.putf(file utl_file.file_type, format text, arg1 text);
ALTER EXTENSION orafce ADD function utl_file.putf(file utl_file.file_type, format text);
ALTER EXTENSION orafce ADD function utl_file.fflush(file utl_file.file_type);
ALTER EXTENSION orafce ADD function utl_file.fclose(file utl_file.file_type);
ALTER EXTENSION orafce ADD function utl_file.fclose_all();
ALTER EXTENSION orafce ADD function utl_file.fremove(location text, filename text);
ALTER EXTENSION orafce ADD function utl_file.frename(location text, filename text, dest_dir text, dest_file text, overwrite boolean);
ALTER EXTENSION orafce ADD function utl_file.frename(location text, filename text, dest_dir text, dest_file text);
ALTER EXTENSION orafce ADD function utl_file.fcopy(src_location text, src_filename text, dest_location text, dest_filename text);
ALTER EXTENSION orafce ADD function utl_file.fcopy(src_location text, src_filename text, dest_location text, dest_filename text, start_line integer);
ALTER EXTENSION orafce ADD function utl_file.fcopy(src_location text, src_filename text, dest_location text, dest_filename text, start_line integer, end_line integer);
ALTER EXTENSION orafce ADD function utl_file.fgetattr(location text, filename text, OUT fexists boolean, OUT file_length bigint, OUT blocksize integer);
ALTER EXTENSION orafce ADD function utl_file.tmpdir();

/* carry all safe directories */
ALTER EXTENSION orafce ADD table utl_file.utl_file_dir;

-- dbms_assert

ALTER EXTENSION orafce ADD schema dbms_assert;
ALTER EXTENSION orafce ADD function dbms_assert.enquote_literal(str varchar);
ALTER EXTENSION orafce ADD function dbms_assert.enquote_name(str varchar, loweralize boolean);
ALTER EXTENSION orafce ADD function dbms_assert.enquote_name(str varchar);
ALTER EXTENSION orafce ADD function dbms_assert.noop(str varchar);
ALTER EXTENSION orafce ADD function dbms_assert.schema_name(str varchar);
ALTER EXTENSION orafce ADD function dbms_assert.object_name(str varchar);
ALTER EXTENSION orafce ADD function dbms_assert.simple_sql_name(str varchar);
ALTER EXTENSION orafce ADD function dbms_assert.qualified_sql_name(str varchar);

ALTER EXTENSION orafce ADD schema plunit;
ALTER EXTENSION orafce ADD function plunit.assert_true(condition boolean);
ALTER EXTENSION orafce ADD function plunit.assert_true(condition boolean, message varchar);
ALTER EXTENSION orafce ADD function plunit.assert_false(condition boolean);
ALTER EXTENSION orafce ADD function plunit.assert_false(condition boolean, message varchar);
ALTER EXTENSION orafce ADD function plunit.assert_null(actual anyelement);
ALTER EXTENSION orafce ADD function plunit.assert_null(actual anyelement, message varchar);
ALTER EXTENSION orafce ADD function plunit.assert_not_null(actual anyelement);
ALTER EXTENSION orafce ADD function plunit.assert_not_null(actual anyelement, message varchar);
ALTER EXTENSION orafce ADD function plunit.assert_equals(expected anyelement, actual anyelement);
ALTER EXTENSION orafce ADD function plunit.assert_equals(expected anyelement, actual anyelement, message varchar);
ALTER EXTENSION orafce ADD function plunit.assert_equals(expected double precision, actual double precision, "range" double precision);
ALTER EXTENSION orafce ADD function plunit.assert_equals(expected double precision, actual double precision, "range" double precision, message varchar);
ALTER EXTENSION orafce ADD function plunit.assert_not_equals(expected anyelement, actual anyelement);
ALTER EXTENSION orafce ADD function plunit.assert_not_equals(expected anyelement, actual anyelement, message varchar);
ALTER EXTENSION orafce ADD function plunit.assert_not_equals(expected double precision, actual double precision, "range" double precision);
ALTER EXTENSION orafce ADD function plunit.assert_not_equals(expected double precision, actual double precision, "range" double precision, message varchar);
ALTER EXTENSION orafce ADD function plunit.fail();
ALTER EXTENSION orafce ADD function plunit.fail(message varchar);

-- dbms_random
ALTER EXTENSION orafce ADD schema dbms_random;
ALTER EXTENSION orafce ADD function dbms_random.initialize(int);
ALTER EXTENSION orafce ADD function dbms_random.normal();
ALTER EXTENSION orafce ADD function dbms_random.random();
ALTER EXTENSION orafce ADD function dbms_random.seed(integer);
ALTER EXTENSION orafce ADD function dbms_random.seed(text);
ALTER EXTENSION orafce ADD function dbms_random.string(opt text, len int);
ALTER EXTENSION orafce ADD function dbms_random.terminate();
ALTER EXTENSION orafce ADD function dbms_random.value(low double precision, high double precision);
ALTER EXTENSION orafce ADD function dbms_random.value();

ALTER EXTENSION orafce ADD function dump(text);
ALTER EXTENSION orafce ADD function dump(text, integer);
ALTER EXTENSION orafce ADD function utl_file.put_line(file utl_file.file_type, buffer anyelement);
ALTER EXTENSION orafce ADD function utl_file.put_line(file utl_file.file_type, buffer anyelement, autoflush bool);
ALTER EXTENSION orafce ADD function pg_catalog.listagg1_transfn(internal, text);
ALTER EXTENSION orafce ADD function pg_catalog.listagg2_transfn(internal, text, text);
ALTER EXTENSION orafce ADD function pg_catalog.listagg_finalfn(internal);

ALTER EXTENSION orafce ADD aggregate pg_catalog.listagg(text);
ALTER EXTENSION orafce ADD aggregate pg_catalog.listagg(text, text);

ALTER EXTENSION orafce ADD function pg_catalog.median4_transfn(internal, real);
ALTER EXTENSION orafce ADD function pg_catalog.median4_finalfn(internal);
ALTER EXTENSION orafce ADD function pg_catalog.median8_transfn(internal, double precision);
ALTER EXTENSION orafce ADD function pg_catalog.median8_finalfn(internal);

ALTER EXTENSION orafce ADD aggregate pg_catalog.median(real);
ALTER EXTENSION orafce ADD aggregate pg_catalog.median(double precision);

