--  REGEXP_INSTR( string text, pattern text ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text)
RETURNS integer
AS 'MODULE_PATHNAME','orafce_regexp_instr_no_start'
LANGUAGE 'c' IMMUTABLE;

--  REGEXP_INSTR( string text, pattern text, position int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer)
RETURNS integer
AS 'MODULE_PATHNAME','orafce_regexp_instr_no_n'
LANGUAGE 'c' IMMUTABLE;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer)
RETURNS integer
AS 'MODULE_PATHNAME','orafce_regexp_instr_no_endoption'
LANGUAGE 'c' IMMUTABLE;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int, return_opt int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer, integer)
RETURNS integer
AS 'MODULE_PATHNAME','orafce_regexp_instr_no_flags'
LANGUAGE 'c' IMMUTABLE;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int, return_opt int, flags text ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer, integer, text)
RETURNS integer
AS 'MODULE_PATHNAME','orafce_regexp_instr_no_subexpr'
LANGUAGE 'c' IMMUTABLE;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int, return_opt int, flags text, group int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer, integer, text, integer)
RETURNS integer
AS 'MODULE_PATHNAME','orafce_regexp_instr'
LANGUAGE 'c' IMMUTABLE;
