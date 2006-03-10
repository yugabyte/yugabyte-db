SET search_path = public;

CREATE OR REPLACE FUNCTION trunc(value date, fmt text)
RETURNS date
AS '$libdir/orafunc','ora_date_trunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION round(value date, fmt text)
RETURNS date
AS '$libdir/orafunc','ora_date_round'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION next_day(value date, weekday text) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION last_day(value date) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION months_between(date1 date, date2 date) 
RETURNS float8
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION add_months(day date, value int) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION trunc(value timestamp with time zone, fmt text) 
RETURNS timestamp with time zone
AS '$libdir/orafunc', 'ora_timestamptz_trunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION round(value timestamp with time zone, fmt text) 
RETURNS timestamp with time zone
AS '$libdir/orafunc','ora_timestamptz_round'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION round(value timestamp with time zone)
RETURNS timestamp with time zone
AS $$ SELECT round($1, 'DDD'); $$
LANGUAGE 'SQL' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION round(value date)
RETURNS date 
AS $$ SELECT $1; $$
LANGUAGE 'SQL' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION trunc(value timestamp with time zone)
RETURNS timestamp with time zone
AS $$ SELECT trunc($1, 'DDD'); $$
LANGUAGE 'SQL' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION trunc(value date)
RETURNS date 
AS $$ SELECT $1; $$
LANGUAGE 'SQL' IMMUTABLE STRICT;

DROP TABLE dual CASCADE;
CREATE TABLE dual(dummy varchar(1));
INSERT INTO dual(dummy) VALUES('X');
REVOKE ALL ON dual FROM PUBLIC;
GRANT SELECT, REFERENCES ON dual TO PUBLIC;
VACUUM ANALYZE dual;

CREATE OR REPLACE FUNCTION protect_table_fx() 
RETURNS TRIGGER 
AS '$libdir/orafunc','ora_protect_table_fx'
LANGUAGE C VOLATILE STRICT;

CREATE TRIGGER protect_dual BEFORE INSERT OR UPDATE OR DELETE
ON dual FOR EACH STATEMENT EXECUTE PROCEDURE protect_table_fx();

-- this packege is emulation of dbms_ouput Oracle packege
-- 

DROP SCHEMA dbms_output CASCADE;

CREATE SCHEMA dbms_output;

CREATE FUNCTION dbms_output.enable(IN buffer_size int4) 
RETURNS void 
AS '$libdir/orafunc','dbms_output_enable' 
LANGUAGE C VOLATILE STRICT;
    
CREATE FUNCTION dbms_output.enable()
RETURNS void 
AS '$libdir/orafunc','dbms_output_enable_default' 
LANGUAGE C VOLATILE STRICT;
    
CREATE FUNCTION dbms_output.disable()
RETURNS void
AS '$libdir/orafunc','dbms_output_disable' 
LANGUAGE C VOLATILE STRICT; 

CREATE FUNCTION dbms_output.serveroutput(IN bool)
RETURNS void
AS '$libdir/orafunc','dbms_output_serveroutput' 
LANGUAGE C VOLATILE STRICT;
    
CREATE FUNCTION dbms_output.put(IN a text)
RETURNS void
AS '$libdir/orafunc','dbms_output_put' 
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION dbms_output.put_line(IN a text)
RETURNS void
AS '$libdir/orafunc','dbms_output_put_line' 
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION dbms_output.new_line()
RETURNS void
AS '$libdir/orafunc','dbms_output_new_line' 
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION dbms_output.get_line(OUT line text, OUT status int4) 
AS '$libdir/orafunc','dbms_output_get_line' 
LANGUAGE C VOLATILE STRICT;
    
CREATE FUNCTION dbms_output.get_lines(OUT lines text[], INOUT numlines int4)
AS '$libdir/orafunc','dbms_output_get_lines' 
LANGUAGE C VOLATILE STRICT;

-- others functions

CREATE OR REPLACE FUNCTION nvl(anyelement, anyelement) 
RETURNS anyelement
AS '$libdir/orafunc','ora_nvl' 
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION nvl2(anyelement, anyelement, anyelement) 
RETURNS anyelement
AS '$libdir/orafunc','ora_nvl2' 
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION concat(text, text) 
RETURNS text 
AS '$libdir/orafunc','ora_concat' 
LANGUAGE C IMMUTABLE;

DROP SCHEMA dbms_pipe CASCADE;

CREATE SCHEMA dbms_pipe;

CREATE FUNCTION dbms_pipe.pack_message(text)
RETURNS void
AS '$libdir/orafunc','dbms_pipe_pack_message' 
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION dbms_pipe.unpack_message()
RETURNS text
AS '$libdir/orafunc','dbms_pipe_unpack_message' 
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION dbms_pipe.receive_message(cstring, int)
RETURNS int
AS '$libdir/orafunc','dbms_pipe_receive_message' 
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION dbms_pipe.send_message(cstring, int)
RETURNS int
AS '$libdir/orafunc','dbms_pipe_send_message' 
LANGUAGE C VOLATILE STRICT;

-- follow package PLVdate emulation

DROP SCHEMA plvdate CASCADE;

CREATE SCHEMA plvdate;

CREATE FUNCTION plvdate.add_bizdays(date, int)
RETURNS date
AS '$libdir/orafunc','plvdate_add_bizdays'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION plvdate.nearest_bizday(date)
RETURNS date
AS '$libdir/orafunc','plvdate_nearest_bizday'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION plvdate.next_bizday(date)
RETURNS date
AS '$libdir/orafunc','plvdate_next_bizday'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION plvdate.bizdays_between(date, date)
RETURNS int
AS '$libdir/orafunc','plvdate_bizdays_between'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION plvdate.prev_bizday(date)
RETURNS date
AS '$libdir/orafunc','plvdate_prev_bizday'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION plvdate.isbizday(date)
RETURNS bool
AS '$libdir/orafunc','plvdate_isbizday'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION plvdate.set_nonbizday(varchar)
RETURNS void
AS '$libdir/orafunc','plvdate_set_nonbizday_dow'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION plvdate.unset_nonbizday(varchar)
RETURNS void
AS '$libdir/orafunc','plvdate_unset_nonbizday_dow'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION plvdate.set_nonbizday(date, bool)
RETURNS void
AS '$libdir/orafunc','plvdate_set_nonbizday_day'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION plvdate.unset_nonbizday(date, bool)
RETURNS void
AS '$libdir/orafunc','plvdate_unset_nonbizday_day'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION plvdate.set_nonbizday(date)
RETURNS bool
AS $$SELECT plvdate.set_nonbizday($1, false);$$
LANGUAGE SQL VOLATILE STRICT;

CREATE FUNCTION plvdate.unset_nonbizday(date)
RETURNS bool
AS $$SELECT plvdate.unset_nonbizday($1, false); $$
LANGUAGE SQL VOLATILE STRICT;

CREATE FUNCTION plvdate.use_easter(bool)
RETURNS void
AS '$libdir/orafunc','plvdate_use_easter'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION plvdate.version()
RETURNS void
AS '$libdir/orafunc','plvdate_version'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION plvdate.default_czech()
RETURNS void
AS '$libdir/orafunc','plvdate_default_czech'
LANGUAGE C VOLATILE STRICT;
