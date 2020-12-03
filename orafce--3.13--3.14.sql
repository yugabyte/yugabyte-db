CREATE OR REPLACE FUNCTION oracle.unistr(text)
RETURNS text
AS 'MODULE_PATHNAME','orafce_unistr'
LANGUAGE 'c';
