CREATE OR REPLACE FUNCTION pg_catalog.trunc(value date, fmt text)
RETURNS date
AS '$libdir/orafunc','ora_date_trunc'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.trunc(date,text) IS 'truncate date according to the specified format';

CREATE OR REPLACE FUNCTION pg_catalog.round(value date, fmt text)
RETURNS date
AS '$libdir/orafunc','ora_date_round'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.round(date, text) IS 'round dates according to the specified format';

CREATE OR REPLACE FUNCTION pg_catalog.next_day(value date, weekday text) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.next_day (date, text) IS 'returns the first weekday that is greather than a date value';

CREATE OR REPLACE FUNCTION pg_catalog.last_day(value date) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.last_day(date) IS 'returns last day of the month based on a date value';

CREATE OR REPLACE FUNCTION pg_catalog.months_between(date1 date, date2 date) 
RETURNS float8
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.months_between(date, date) IS 'returns the number of months between date1 and date2';

CREATE OR REPLACE FUNCTION pg_catalog.add_months(day date, value int) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.add_months(date, int) IS 'returns date plus n months';

CREATE OR REPLACE FUNCTION pg_catalog.trunc(value timestamp with time zone, fmt text) 
RETURNS timestamp with time zone
AS '$libdir/orafunc', 'ora_timestamptz_trunc'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.trunc(timestamp with time zone, text) IS 'truncate date according to the specified format';

CREATE OR REPLACE FUNCTION pg_catalog.round(value timestamp with time zone, fmt text) 
RETURNS timestamp with time zone
AS '$libdir/orafunc','ora_timestamptz_round'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.round(timestamp with time zone, text) IS 'round dates according to the specified format';

CREATE OR REPLACE FUNCTION pg_catalog.round(value timestamp with time zone)
RETURNS timestamp with time zone
AS $$ SELECT pg_catalog.round($1, 'DDD'); $$
LANGUAGE 'SQL' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.round(timestamp with time zone) IS 'round date';

CREATE OR REPLACE FUNCTION pg_catalog.round(value date)
RETURNS date 
AS $$ SELECT $1; $$
LANGUAGE 'SQL' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.round(value date)IS 'round date';

CREATE OR REPLACE FUNCTION pg_catalog.trunc(value timestamp with time zone)
RETURNS timestamp with time zone
AS $$ SELECT pg_catalog.trunc($1, 'DDD'); $$
LANGUAGE 'SQL' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.trunc(timestamp with time zone) IS 'truncate date';

CREATE OR REPLACE FUNCTION pg_catalog.trunc(value date)
RETURNS date 
AS $$ SELECT $1; $$
LANGUAGE 'SQL' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.trunc(date) IS 'truncate date';

CREATE OR REPLACE FUNCTION pg_catalog.nlssort(text, text)
RETURNS bytea
AS '$libdir/orafunc', 'ora_nlssort'
LANGUAGE 'C' IMMUTABLE;
COMMENT ON FUNCTION pg_catalog.nlssort(text, text) IS '';

CREATE OR REPLACE FUNCTION pg_catalog.nlssort(text)
RETURNS bytea
AS $$ SELECT pg_catalog.nlssort($1, null); $$
LANGUAGE 'SQL' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.nlssort(text)IS '';

CREATE OR REPLACE FUNCTION pg_catalog.set_nls_sort(text)
RETURNS void
AS '$libdir/orafunc', 'ora_set_nls_sort'
LANGUAGE 'C' IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.set_nls_sort(text) IS '';

CREATE OR REPLACE FUNCTION pg_catalog.instr(str text, patt text, start int, nth int)
RETURNS int
AS '$libdir/orafunc','plvstr_instr4'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION pg_catalog.instr(text, text, int, int) IS '';

CREATE OR REPLACE FUNCTION pg_catalog.instr(str text, patt text, start int)
RETURNS int
AS '$libdir/orafunc','plvstr_instr3'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION pg_catalog.instr(text, text, int) IS '';

CREATE OR REPLACE FUNCTION pg_catalog.instr(str text, patt text)
RETURNS int
AS '$libdir/orafunc','plvstr_instr2'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION pg_catalog.instr(text, text) IS '';

CREATE OR REPLACE FUNCTION pg_catalog.reverse(str text)
RETURNS text
AS $$ SELECT plvstr.rvrs($1,1,NULL);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION pg_catalog.reverse(text) IS '';

CREATE OR REPLACE FUNCTION pg_catalog.lnnvl(bool)
RETURNS bool
AS '$libdir/orafunc','ora_lnnvl'
LANGUAGE C STABLE;
COMMENT ON FUNCTION pg_catalog.lnnvl(bool) IS '';


-- can't overwrite PostgreSQL functions!!!!

DROP SCHEMA orafce CASCADE;
CREATE SCHEMA orafce;

CREATE OR REPLACE FUNCTION orafce.substr(str text, start int)
RETURNS text
AS '$libdir/orafunc','oracle_substr2'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION orafce.substr(text, int) IS '';

CREATE OR REPLACE FUNCTION orafce.substr(str text, start int, len int)
RETURNS text
AS '$libdir/orafunc','oracle_substr3'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION orafce.substr(text, int, int) IS '';

DROP TABLE public.dual CASCADE;
CREATE TABLE public.dual(dummy varchar(1));
INSERT INTO public.dual(dummy) VALUES('X');
CREATE RULE noins_dual AS ON INSERT TO public.dual DO NOTHING;
CREATE RULE nodel_dual AS ON DELETE TO public.dual DO NOTHING;
CREATE RULE noupd_dual AS ON UPDATE TO public.dual DO NOTHING;
REVOKE ALL ON public.dual FROM PUBLIC;
GRANT SELECT, REFERENCES ON public.dual TO PUBLIC;
VACUUM ANALYZE public.dual;

-- this packege is emulation of dbms_ouput Oracle packege
-- 

DROP SCHEMA dbms_output CASCADE;

CREATE SCHEMA dbms_output;

CREATE OR REPLACE FUNCTION dbms_output.enable(IN buffer_size int4) 
RETURNS void 
AS '$libdir/orafunc','dbms_output_enable' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_output.enable(IN int4) IS '';
    
CREATE OR REPLACE FUNCTION dbms_output.enable()
RETURNS void 
AS '$libdir/orafunc','dbms_output_enable_default' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_output.enable() IS '';
    
CREATE OR REPLACE FUNCTION dbms_output.disable()
RETURNS void
AS '$libdir/orafunc','dbms_output_disable' 
LANGUAGE C VOLATILE STRICT; 
COMMENT ON FUNCTION dbms_output.disable() IS '';

CREATE OR REPLACE FUNCTION dbms_output.serveroutput(IN bool)
RETURNS void
AS '$libdir/orafunc','dbms_output_serveroutput' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_output.serveroutput(IN bool) IS '';
    
CREATE OR REPLACE FUNCTION dbms_output.put(IN a text)
RETURNS void
AS '$libdir/orafunc','dbms_output_put' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_output.put(IN text) IS '';

CREATE OR REPLACE FUNCTION dbms_output.put_line(IN a text)
RETURNS void
AS '$libdir/orafunc','dbms_output_put_line' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_output.put_line(IN text) IS '';

CREATE OR REPLACE FUNCTION dbms_output.new_line()
RETURNS void
AS '$libdir/orafunc','dbms_output_new_line' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_output.new_line() IS '';

CREATE OR REPLACE FUNCTION dbms_output.get_line(OUT line text, OUT status int4) 
AS '$libdir/orafunc','dbms_output_get_line' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_output.get_line(OUT text, OUT int4) IS '';

    
CREATE OR REPLACE FUNCTION dbms_output.get_lines(OUT lines text[], INOUT numlines int4)
AS '$libdir/orafunc','dbms_output_get_lines' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_output.get_lines(OUT text[], INOUT int4) IS '';


-- others functions

CREATE OR REPLACE FUNCTION nvl(anyelement, anyelement) 
RETURNS anyelement
AS '$libdir/orafunc','ora_nvl' 
LANGUAGE C IMMUTABLE;
COMMENT ON FUNCTION nvl(anyelement, anyelement) IS '';

CREATE OR REPLACE FUNCTION nvl2(anyelement, anyelement, anyelement) 
RETURNS anyelement
AS '$libdir/orafunc','ora_nvl2' 
LANGUAGE C IMMUTABLE;
COMMENT ON FUNCTION nvl2(anyelement, anyelement, anyelement) IS '';

CREATE OR REPLACE FUNCTION concat(text, text) 
RETURNS text 
AS '$libdir/orafunc','ora_concat' 
LANGUAGE C IMMUTABLE;
COMMENT ON FUNCTION concat(text, text) IS '';

DROP SCHEMA dbms_pipe CASCADE;

CREATE SCHEMA dbms_pipe;

CREATE OR REPLACE FUNCTION dbms_pipe.pack_message(text)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_pack_message_text' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.pack_message(text) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.unpack_message_text()
RETURNS text
AS '$libdir/orafunc','dbms_pipe_unpack_message_text' 
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_pipe.unpack_message_text() IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.receive_message(text, int)
RETURNS int
AS '$libdir/orafunc','dbms_pipe_receive_message' 
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_pipe.receive_message(text, int) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.receive_message(text)
RETURNS int
AS $$SELECT dbms_pipe.receive_message($1,NULL::int);$$
LANGUAGE SQL VOLATILE;
COMMENT ON FUNCTION dbms_pipe.receive_message(text) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.send_message(text, int, int)
RETURNS int
AS '$libdir/orafunc','dbms_pipe_send_message' 
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_pipe.send_message(text, int, int) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.send_message(text, int)
RETURNS int
AS $$SELECT dbms_pipe.send_message($1,$2,NULL);$$
LANGUAGE SQL VOLATILE;
COMMENT ON FUNCTION dbms_pipe.send_message(text, int) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.send_message(text)
RETURNS int
AS $$SELECT dbms_pipe.send_message($1,NULL,NULL);$$
LANGUAGE SQL VOLATILE;
COMMENT ON FUNCTION dbms_pipe.send_message(text) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.unique_session_name()
RETURNS varchar
AS '$libdir/orafunc','dbms_pipe_unique_session_name' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.unique_session_name() IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.__list_pipes()
RETURNS SETOF RECORD
AS '$libdir/orafunc','dbms_pipe_list_pipes' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.__list_pipes() IS '';

CREATE VIEW dbms_pipe.db_pipes 
AS SELECT * FROM dbms_pipe.__list_pipes() AS (Name varchar, Items int, Size int, "limit" int, "private" bool, "owner" varchar);

CREATE OR REPLACE FUNCTION dbms_pipe.next_item_type()
RETURNS int
AS '$libdir/orafunc','dbms_pipe_next_item_type' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.next_item_type() IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.create_pipe(text, int, bool)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_create_pipe' 
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_pipe.create_pipe(text, int, bool) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.create_pipe(text, int)
RETURNS void
AS $$BEGIN PERFORM dbms_pipe.create_pipe($1,$2,false); END$$
LANGUAGE plpgsql VOLATILE;
COMMENT ON FUNCTION dbms_pipe.create_pipe(text, int) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.create_pipe(text)
RETURNS void
AS $$BEGIN PERFORM dbms_pipe.create_pipe($1,NULL,false); END$$
LANGUAGE plpgsql VOLATILE;
COMMENT ON FUNCTION dbms_pipe.create_pipe(text) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.reset_buffer()
RETURNS void
AS '$libdir/orafunc','dbms_pipe_reset_buffer' 
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_pipe.reset_buffer() IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.purge(text)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_purge' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.purge(text) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.remove_pipe(text)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_remove_pipe' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.remove_pipe(text) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.pack_message(date)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_pack_message_date' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.pack_message(date) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.unpack_message_date()
RETURNS date
AS '$libdir/orafunc','dbms_pipe_unpack_message_date' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.unpack_message_date() IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.pack_message(timestamp with time zone)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_pack_message_timestamp' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.pack_message(timestamp with time zone) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.unpack_message_timestamp()
RETURNS timestamp with time zone
AS '$libdir/orafunc','dbms_pipe_unpack_message_timestamp' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.unpack_message_timestamp() IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.pack_message(numeric)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_pack_message_number' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.pack_message(numeric) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.unpack_message_number()
RETURNS numeric
AS '$libdir/orafunc','dbms_pipe_unpack_message_number' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.unpack_message_number() IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.pack_message(integer)
RETURNS void
AS $$BEGIN PERFORM dbms_pipe.pack_message($1::numeric); END$$
LANGUAGE plpgsql VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.pack_message(integer) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.pack_message(bigint)
RETURNS void
AS $$BEGIN PERFORM dbms_pipe.pack_message($1::numeric); END$$
LANGUAGE plpgsql VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.pack_message(bigint) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.pack_message(bytea)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_pack_message_bytea' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.pack_message(bytea) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.unpack_message_bytea()
RETURNS bytea
AS '$libdir/orafunc','dbms_pipe_unpack_message_bytea' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.unpack_message_bytea() IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.pack_message(record)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_pack_message_record' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.pack_message(record) IS '';

CREATE OR REPLACE FUNCTION dbms_pipe.unpack_message_record()
RETURNS record
AS '$libdir/orafunc','dbms_pipe_unpack_message_record' 
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_pipe.unpack_message_record() IS '';



-- follow package PLVdate emulation

DROP SCHEMA plvdate CASCADE;

CREATE SCHEMA plvdate;

CREATE OR REPLACE FUNCTION plvdate.add_bizdays(date, int)
RETURNS date
AS '$libdir/orafunc','plvdate_add_bizdays'
LANGUAGE C IMMUTABLE STRICT;
COMMENT ON FUNCTION plvdate.add_bizdays(date, int) IS '';

CREATE OR REPLACE FUNCTION plvdate.nearest_bizday(date)
RETURNS date
AS '$libdir/orafunc','plvdate_nearest_bizday'
LANGUAGE C IMMUTABLE STRICT;
COMMENT ON FUNCTION plvdate.nearest_bizday(date) IS '';

CREATE OR REPLACE FUNCTION plvdate.next_bizday(date)
RETURNS date
AS '$libdir/orafunc','plvdate_next_bizday'
LANGUAGE C IMMUTABLE STRICT;
COMMENT ON FUNCTION plvdate.next_bizday(date) IS '';

CREATE OR REPLACE FUNCTION plvdate.bizdays_between(date, date)
RETURNS int
AS '$libdir/orafunc','plvdate_bizdays_between'
LANGUAGE C IMMUTABLE STRICT;
COMMENT ON FUNCTION plvdate.bizdays_between(date, date) IS '';

CREATE OR REPLACE FUNCTION plvdate.prev_bizday(date)
RETURNS date
AS '$libdir/orafunc','plvdate_prev_bizday'
LANGUAGE C IMMUTABLE STRICT;
COMMENT ON FUNCTION plvdate.prev_bizday(date) IS '';

CREATE OR REPLACE FUNCTION plvdate.isbizday(date)
RETURNS bool
AS '$libdir/orafunc','plvdate_isbizday'
LANGUAGE C IMMUTABLE STRICT;
COMMENT ON FUNCTION plvdate.isbizday(date) IS '';

CREATE OR REPLACE FUNCTION plvdate.set_nonbizday(text)
RETURNS void
AS '$libdir/orafunc','plvdate_set_nonbizday_dow'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.set_nonbizday(text) IS '';

CREATE OR REPLACE FUNCTION plvdate.unset_nonbizday(text)
RETURNS void
AS '$libdir/orafunc','plvdate_unset_nonbizday_dow'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.unset_nonbizday(text) IS '';

CREATE OR REPLACE FUNCTION plvdate.set_nonbizday(date, bool)
RETURNS void
AS '$libdir/orafunc','plvdate_set_nonbizday_day'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.set_nonbizday(date, bool) IS '';

CREATE OR REPLACE FUNCTION plvdate.unset_nonbizday(date, bool)
RETURNS void
AS '$libdir/orafunc','plvdate_unset_nonbizday_day'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.unset_nonbizday(date, bool) IS '';

CREATE OR REPLACE FUNCTION plvdate.set_nonbizday(date)
RETURNS bool
AS $$SELECT plvdate.set_nonbizday($1, false); SELECT NULL::boolean;$$
LANGUAGE SQL VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.set_nonbizday(date) IS '';

CREATE OR REPLACE FUNCTION plvdate.unset_nonbizday(date)
RETURNS bool
AS $$SELECT plvdate.unset_nonbizday($1, false); SELECT NULL::boolean;$$
LANGUAGE SQL VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.unset_nonbizday(date) IS '';

CREATE OR REPLACE FUNCTION plvdate.use_easter(bool)
RETURNS void
AS '$libdir/orafunc','plvdate_use_easter'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.use_easter(bool) IS '';

CREATE OR REPLACE FUNCTION plvdate.use_easter()
RETURNS bool
AS $$SELECT plvdate.use_easter(true); SELECT NULL::boolean;$$
LANGUAGE SQL VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.use_easter() IS '';

CREATE OR REPLACE FUNCTION plvdate.unuse_easter()
RETURNS bool
AS $$SELECT plvdate.use_easter(false); SELECT NULL::boolean;$$
LANGUAGE SQL VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.unuse_easter() IS '';

CREATE OR REPLACE FUNCTION plvdate.using_easter()
RETURNS bool
AS '$libdir/orafunc','plvdate_using_easter'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.using_easter() IS '';

CREATE OR REPLACE FUNCTION plvdate.include_start(bool)
RETURNS void
AS '$libdir/orafunc','plvdate_include_start'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.include_start(bool) IS '';

CREATE OR REPLACE FUNCTION plvdate.include_start()
RETURNS bool
AS $$SELECT plvdate.include_start(true); SELECT NULL::boolean;$$
LANGUAGE SQL VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.include_start() IS '';

CREATE OR REPLACE FUNCTION plvdate.noinclude_start()
RETURNS bool
AS $$SELECT plvdate.include_start(false); SELECT NULL::boolean;$$
LANGUAGE SQL VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.noinclude_start() IS '';

CREATE OR REPLACE FUNCTION plvdate.including_start()
RETURNS bool
AS '$libdir/orafunc','plvdate_including_start'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.including_start() IS '';

CREATE OR REPLACE FUNCTION plvdate.version()
RETURNS cstring
AS '$libdir/orafunc','plvdate_version'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.version() IS '';

CREATE OR REPLACE FUNCTION plvdate.default_holydays(text)
RETURNS void
AS '$libdir/orafunc','plvdate_default_holydays'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.default_holydays(text) IS '';

CREATE OR REPLACE FUNCTION plvdate.days_inmonth(date)
RETURNS integer
AS '$libdir/orafunc','plvdate_days_inmonth'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.days_inmonth(date) IS '';

CREATE OR REPLACE FUNCTION plvdate.isleapyear(date)
RETURNS bool
AS '$libdir/orafunc','plvdate_isleapyear'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION plvdate.isleapyear(date) IS '';


-- PLVstr package

DROP SCHEMA plvstr CASCADE;

CREATE SCHEMA plvstr;

CREATE OR REPLACE FUNCTION plvstr.normalize(str text)
RETURNS varchar
AS '$libdir/orafunc','plvstr_normalize'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.normalize(text) IS '';

CREATE OR REPLACE FUNCTION plvstr.is_prefix(str text, prefix text, cs bool)
RETURNS bool
AS '$libdir/orafunc','plvstr_is_prefix_text'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.is_prefix(text, text, bool) IS '';

CREATE OR REPLACE FUNCTION plvstr.is_prefix(str text, prefix text)
RETURNS bool
AS $$ SELECT plvstr.is_prefix($1,$2,true);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.is_prefix(text, text) IS '';

CREATE OR REPLACE FUNCTION plvstr.is_prefix(str int, prefix int)
RETURNS bool
AS '$libdir/orafunc','plvstr_is_prefix_int'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.is_prefix(int, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.is_prefix(str bigint, prefix bigint)
RETURNS bool
AS '$libdir/orafunc','plvstr_is_prefix_int64'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.is_prefix(bigint, bigint) IS '';

CREATE OR REPLACE FUNCTION plvstr.substr(str text, start int, len int)
RETURNS varchar
AS '$libdir/orafunc','plvstr_substr3'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.substr(text, int, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.substr(str text, start int)
RETURNS varchar
AS '$libdir/orafunc','plvstr_substr2'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.substr(text, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.instr(str text, patt text, start int, nth int)
RETURNS int
AS '$libdir/orafunc','plvstr_instr4'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.instr(text, text, int, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.instr(str text, patt text, start int)
RETURNS int
AS '$libdir/orafunc','plvstr_instr3'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.instr(text, text, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.instr(str text, patt text)
RETURNS int
AS '$libdir/orafunc','plvstr_instr2'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.instr(text, text) IS '';

CREATE OR REPLACE FUNCTION plvstr.lpart(str text, div text, start int, nth int, all_if_notfound bool)
RETURNS text
AS '$libdir/orafunc','plvstr_lpart'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.lpart(text, text, int, int, bool) IS '';

CREATE OR REPLACE FUNCTION plvstr.lpart(str text, div text, start int, nth int)
RETURNS text
AS $$ SELECT plvstr.lpart($1,$2, $3, $4, false); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.lpart(text, text, int, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.lpart(str text, div text, start int)
RETURNS text
AS $$ SELECT plvstr.lpart($1,$2, $3, 1, false); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.lpart(text, text, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.lpart(str text, div text)
RETURNS text
AS $$ SELECT plvstr.lpart($1,$2, 1, 1, false); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.lpart(text, text) IS '';

CREATE OR REPLACE FUNCTION plvstr.rpart(str text, div text, start int, nth int, all_if_notfound bool)
RETURNS text
AS '$libdir/orafunc','plvstr_rpart'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.rpart(text, text, int, int, bool) IS '';

CREATE OR REPLACE FUNCTION plvstr.rpart(str text, div text, start int, nth int)
RETURNS text
AS $$ SELECT plvstr.rpart($1,$2, $3, $4, false); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.rpart(text, text, int, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.rpart(str text, div text, start int)
RETURNS text
AS $$ SELECT plvstr.rpart($1,$2, $3, 1, false); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.rpart(text, text, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.rpart(str text, div text)
RETURNS text
AS $$ SELECT plvstr.rpart($1,$2, 1, 1, false); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.rpart(text, text) IS '';

CREATE OR REPLACE FUNCTION plvstr.lstrip(str text, substr text, num int)
RETURNS text
AS '$libdir/orafunc','plvstr_lstrip'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.lstrip(text, text, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.lstrip(str text, substr text)
RETURNS text
AS $$ SELECT plvstr.lstrip($1, $2, 1); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.lstrip(text, text) IS '';

CREATE OR REPLACE FUNCTION plvstr.rstrip(str text, substr text, num int)
RETURNS text
AS '$libdir/orafunc','plvstr_rstrip'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.rstrip(text, text, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.rstrip(str text, substr text)
RETURNS text
AS $$ SELECT plvstr.rstrip($1, $2, 1); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.rstrip(text, text) IS '';

CREATE OR REPLACE FUNCTION plvstr.rvrs(str text, start int, _end int)
RETURNS text
AS '$libdir/orafunc','plvstr_rvrs'
LANGUAGE C STABLE;
COMMENT ON FUNCTION plvstr.rvrs(text, int, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.rvrs(str text, start int)
RETURNS text
AS $$ SELECT plvstr.rvrs($1,$2,NULL);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.rvrs(text, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.rvrs(str text)
RETURNS text
AS $$ SELECT plvstr.rvrs($1,1,NULL);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvstr.rvrs(text) IS '';

DROP SCHEMA plvchr CASCADE;

CREATE SCHEMA plvchr;

CREATE OR REPLACE FUNCTION plvchr.nth(str text, n int)
RETURNS text
AS '$libdir/orafunc','plvchr_nth'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvchr.nth(text, int) IS '';

CREATE OR REPLACE FUNCTION plvchr.first(str text)
RETURNS varchar
AS '$libdir/orafunc','plvchr_first'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvchr.first(text) IS '';

CREATE OR REPLACE FUNCTION plvchr.last(str text)
RETURNS varchar
AS '$libdir/orafunc','plvchr_last'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvchr.last(text) IS '';

CREATE OR REPLACE FUNCTION plvchr._is_kind(str text, kind int)
RETURNS bool
AS '$libdir/orafunc','plvchr_is_kind_a'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvchr._is_kind(text, int) IS '';

CREATE OR REPLACE FUNCTION plvchr._is_kind(c int, kind int)
RETURNS bool
AS '$libdir/orafunc','plvchr_is_kind_i'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvchr._is_kind(int, int) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_blank(c int)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 1);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_blank(int) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_blank(c text)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 1);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_blank(text) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_digit(c int)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 2);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_digit(int) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_digit(c text)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 2);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_digit(text) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_quote(c int)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 3);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_quote(int) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_quote(c text)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 3);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_quote(text) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_other(c int)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 4);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_other(int) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_other(c text)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 4);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_other(text) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_letter(c int)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 5);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_letter(int) IS '';

CREATE OR REPLACE FUNCTION plvchr.is_letter(c text)
RETURNS BOOL
AS $$ SELECT plvchr._is_kind($1, 5);$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.is_letter(text) IS '';

CREATE OR REPLACE FUNCTION plvchr.char_name(c text)
RETURNS varchar
AS '$libdir/orafunc','plvchr_char_name'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvchr.char_name(text) IS '';

CREATE OR REPLACE FUNCTION plvstr.left(str text, n int)
RETURNS varchar
AS '$libdir/orafunc', 'plvstr_left'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.left(text, int) IS '';

CREATE OR REPLACE FUNCTION plvstr.right(str text, n int)
RETURNS varchar
AS '$libdir/orafunc','plvstr_right'
LANGUAGE C STABLE STRICT;
COMMENT ON FUNCTION plvstr.right(text, int) IS '';

CREATE OR REPLACE FUNCTION plvchr.quoted1(str text)
RETURNS varchar
AS $$SELECT E'\''||$1||E'\'';$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.quoted1(text) IS '';

CREATE OR REPLACE FUNCTION plvchr.quoted2(str text)
RETURNS varchar
AS $$SELECT '"'||$1||'"';$$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.quoted2(text) IS '';

CREATE OR REPLACE FUNCTION plvchr.stripped(str text, char_in text)
RETURNS varchar
AS $$ SELECT TRANSLATE($1, 'A'||$2, 'A'); $$
LANGUAGE SQL STABLE STRICT;
COMMENT ON FUNCTION plvchr.stripped(text, text) IS '';

-- dbms_alert

DROP SCHEMA dbms_alert CASCADE;

CREATE SCHEMA dbms_alert;

CREATE OR REPLACE FUNCTION dbms_alert.register(name text)
RETURNS void
AS '$libdir/orafunc','dbms_alert_register'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_alert.register(text) IS '';

CREATE OR REPLACE FUNCTION dbms_alert.remove(name text)
RETURNS void 
AS '$libdir/orafunc','dbms_alert_remove'
LANGUAGE C VOLATILE STRICT;
COMMENT ON FUNCTION dbms_alert.remove(text) IS '';

CREATE OR REPLACE FUNCTION dbms_alert.removeall()
RETURNS void
AS '$libdir/orafunc','dbms_alert_removeall'
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_alert.removeall() IS '';

CREATE OR REPLACE FUNCTION dbms_alert._signal(name text, message text)
RETURNS void
AS '$libdir/orafunc','dbms_alert_signal'
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_alert._signal(text, text) IS '';

CREATE OR REPLACE FUNCTION dbms_alert.waitany(OUT name text, OUT message text, OUT status integer, timeout float8)
RETURNS record 
AS '$libdir/orafunc','dbms_alert_waitany'
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_alert.waitany(OUT text, OUT text, OUT integer, float8) IS '';

CREATE OR REPLACE FUNCTION dbms_alert.waitone(name text, OUT message text, OUT status integer, timeout float8)
RETURNS record
AS '$libdir/orafunc','dbms_alert_waitone'
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_alert.waitone(text, OUT text, OUT integer, float8) IS '';

CREATE OR REPLACE FUNCTION dbms_alert.set_defaults(sensitivity float8)
RETURNS void
AS '$libdir/orafunc','dbms_alert_set_defaults'
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_alert.set_defaults(float8) IS '';

CREATE OR REPLACE FUNCTION dbms_alert._defered_signal() RETURNS trigger AS $$
BEGIN
  PERFORM dbms_alert._signal(NEW.event, NEW.message);
  DELETE FROM ora_alerts WHERE oid=NEW.oid;
  RETURN NEW;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER VOLATILE;

CREATE OR REPLACE FUNCTION dbms_alert.signal(_event text, _message text) RETURNS void AS $$
BEGIN
  PERFORM 1 FROM pg_catalog.pg_class c
            WHERE pg_catalog.pg_table_is_visible(c.oid)
            AND c.relkind='r' AND c.relname = 'ora_alerts';
  IF NOT FOUND THEN
    CREATE TEMP TABLE ora_alerts(event text, message text) WITH OIDS;
    REVOKE ALL ON TABLE ora_alerts FROM PUBLIC;
    CREATE CONSTRAINT TRIGGER ora_alert_signal AFTER INSERT ON ora_alerts
      INITIALLY DEFERRED FOR EACH ROW EXECUTE PROCEDURE dbms_alert._defered_signal();
  END IF;
  INSERT INTO ora_alerts(event, message) VALUES(_event, _message);
END;
$$ LANGUAGE plpgsql VOLATILE SECURITY DEFINER;
COMMENT ON FUNCTION dbms_alert.signal(text, text) IS '';

GRANT USAGE ON SCHEMA dbms_pipe TO PUBLIC;
GRANT USAGE ON SCHEMA dbms_alert TO PUBLIC;
GRANT USAGE ON SCHEMA plvdate TO PUBLIC;
GRANT USAGE ON SCHEMA plvstr TO PUBLIC;
GRANT USAGE ON SCHEMA plvchr TO PUBLIC;
GRANT USAGE ON SCHEMA dbms_output TO PUBLIC;

GRANT SELECT ON dbms_pipe.db_pipes to public;
