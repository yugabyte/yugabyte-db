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

-- REGEXP_REPLACE( string text, pattern text, replace_string text ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text)
RETURNS text
AS 'MODULE_PATHNAME','orafce_textregexreplace_noopt'
LANGUAGE 'c' IMMUTABLE;

-- REGEXP_REPLACE( string text, pattern text, replace_string text, position int ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text, integer)
RETURNS text
AS 'MODULE_PATHNAME','orafce_textregexreplace_extended_no_n'
LANGUAGE 'c' IMMUTABLE;

-- REGEXP_REPLACE( string text, pattern text, replace_string text, position int, occurence int ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text, integer, integer)
RETURNS text
AS 'MODULE_PATHNAME','orafce_textregexreplace_extended_no_flags'
LANGUAGE 'c' IMMUTABLE;

-- REGEXP_REPLACE( string text, pattern text, replace_string text, position int, occurence int, flags text ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text, integer, integer, text)
RETURNS text
AS 'MODULE_PATHNAME','orafce_textregexreplace_extended'
LANGUAGE 'c' IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text, text)
RETURNS text
AS 'MODULE_PATHNAME','orafce_textregexreplace'
LANGUAGE 'c' IMMUTABLE;
