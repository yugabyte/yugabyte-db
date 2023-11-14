ALTER FUNCTION oracle.btrim(char) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(char, char) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(char, text) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(char, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(char, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(text) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(text, char) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(text, text) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(text, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(text, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.varchar2, char) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.varchar2, text) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.varchar2, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.varchar2, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.nvarchar2, char) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.nvarchar2, text) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.nvarchar2, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.btrim(oracle.nvarchar2, oracle.nvarchar2) PARALLEL SAFE;

ALTER FUNCTION oracle.ltrim(char, char) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(char, text) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(char, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(char, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(char) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(text, char) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(text, text) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(text, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(text, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(text) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.varchar2, char) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.varchar2, text) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.varchar2, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.varchar2, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.nvarchar2, char) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.nvarchar2, text) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.nvarchar2, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.nvarchar2, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.ltrim(oracle.nvarchar2) PARALLEL SAFE;

ALTER FUNCTION oracle.rtrim(char, char) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(char, text) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(char, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(char, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(char) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(text, char) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(text, text) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(text, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(text, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(text) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.varchar2, char) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.varchar2, text) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.varchar2, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.varchar2, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.nvarchar2, char) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.nvarchar2, text) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.nvarchar2, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.nvarchar2, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rtrim(oracle.nvarchar2) PARALLEL SAFE;


ALTER FUNCTION oracle.lpad(char, integer, char) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(char, integer, text) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(char, integer, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(char, integer, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(char, integer) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(text, integer, char) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.varchar2, integer, char) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.nvarchar2, integer, char) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(text, integer, text) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(text, integer, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(text, integer, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(text, integer) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.varchar2, integer, text) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.varchar2, integer, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.varchar2, integer, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.varchar2, integer) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.nvarchar2, integer, text) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.nvarchar2, integer, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.nvarchar2, integer, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(oracle.nvarchar2, integer) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(int, int, int) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(bigint, int, int) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(smallint, int, int) PARALLEL SAFE;
ALTER FUNCTION oracle.lpad(numeric, int, int) PARALLEL SAFE;

ALTER FUNCTION oracle.rpad(text, integer) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(character, integer) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.varchar2, integer) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.nvarchar2, integer) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(text, integer, text) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(text, integer, character)   PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(text, integer, oracle.varchar2)  PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(text, integer, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(character, integer, text) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(character, integer, character) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(character, integer, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(character, integer, oracle.nvarchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.varchar2, integer, text) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.varchar2, integer, character) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.varchar2, integer, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.varchar2, integer, oracle.nvarchar2) PARALLEL SAFE; 
ALTER FUNCTION oracle.rpad(oracle.nvarchar2, integer, text) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.nvarchar2, integer, character) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.nvarchar2, integer, oracle.varchar2) PARALLEL SAFE;
ALTER FUNCTION oracle.rpad(oracle.nvarchar2, integer, oracle.nvarchar2) PARALLEL SAFE;

ALTER FUNCTION oracle.to_char(num bigint) PARALLEL SAFE;
ALTER FUNCTION oracle.to_char(num smallint) PARALLEL SAFE;
ALTER FUNCTION oracle.to_char(num integer) PARALLEL SAFE;
ALTER FUNCTION oracle.to_char(num real) PARALLEL SAFE;
ALTER FUNCTION oracle.to_char(num double precision) PARALLEL SAFE;
ALTER FUNCTION oracle.to_char(timestamp without time zone) PARALLEL SAFE;
ALTER FUNCTION oracle.to_char(num numeric) PARALLEL SAFE;

ALTER FUNCTION oracle.to_number(str text) PARALLEL SAFE;
ALTER FUNCTION oracle.to_number(numeric) PARALLEL SAFE;
ALTER FUNCTION oracle.to_number(numeric,numeric) PARALLEL SAFE;


CREATE FUNCTION oracle.to_char(str text)
RETURNS text
AS $$
select str;
$$ LANGUAGE SQL IMMUTABLE STRICT PARALLEL SAFE;
COMMENT ON FUNCTION oracle.to_char(text) IS 'Convert string to string';