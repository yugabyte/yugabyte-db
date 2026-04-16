CREATE OR REPLACE FUNCTION oracle.replace_empty_strings()
RETURNS TRIGGER
AS 'MODULE_PATHNAME','orafce_replace_empty_strings'
LANGUAGE 'c';

CREATE OR REPLACE FUNCTION oracle.replace_null_strings()
RETURNS TRIGGER
AS 'MODULE_PATHNAME','orafce_replace_null_strings'
LANGUAGE 'c';
