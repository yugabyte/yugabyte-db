-- Translate Oracle regexp modifier into PostgreSQl ones
-- Append the global modifier if $2 is true. Used internally
-- by regexp_*() functions bellow.
CREATE OR REPLACE FUNCTION oracle.translate_oracle_modifiers(text, bool)
RETURNS text
AS $$
DECLARE
  modifiers text := '';
BEGIN
  IF $1 IS NOT NULL THEN
    -- Check that we don't have modifier not supported by Oracle
    IF $1 ~ '[^icnsmx]' THEN
      -- Modifier 's' is not supported by Oracle but it is a synonym
      -- of 'n', we translate 'n' into 's' bellow. It is safe to allow it.
      RAISE EXCEPTION 'argument ''flags'' has unsupported modifier(s).';
    END IF;
    -- Oracle 'n' modifier correspond to 's' POSIX modifier
    -- Oracle 'm' modifier correspond to 'n' POSIX modifier
    modifiers := translate($1, 'nm', 'sn');
  END IF;
  IF $2 THEN
    modifiers := modifiers || 'g';
  END IF;
  RETURN modifiers;
END;
$$
LANGUAGE plpgsql;

-- REGEXP_LIKE( string text, pattern text) -> boolean
-- If one of the param is NULL returns NULL, declared STRICT
CREATE OR REPLACE FUNCTION oracle.regexp_like(text, text)
RETURNS boolean
AS $$
  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  SELECT CASE WHEN (count(*) > 0) THEN true ELSE false END FROM regexp_matches($1, $2, 'p');
$$
LANGUAGE 'sql' STRICT;

-- REGEXP_LIKE( string text, pattern text, flags text ) -> boolean
CREATE OR REPLACE FUNCTION oracle.regexp_like(text, text, text)
RETURNS boolean
AS $$
DECLARE
  modifiers text;
BEGIN
  -- Only modifier can be NULL
  IF $1 IS NULL OR $2 IS NULL THEN
    RETURN NULL;
  END IF;
  modifiers := oracle.translate_oracle_modifiers($3, false);
  IF (regexp_matches($1, $2, modifiers))[1] IS NOT NULL THEN
    RETURN true;
  END IF;
  RETURN false;
END;
$$
LANGUAGE plpgsql;

-- REGEXP_COUNT( string text, pattern text ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_count(text, text)
RETURNS integer
AS $$
  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  SELECT count(*)::integer FROM regexp_matches($1, $2, 'pg');
$$
LANGUAGE 'sql' STRICT;

-- REGEXP_COUNT( string text, pattern text, position int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_count(text, text, integer)
RETURNS integer
AS $$
DECLARE
  v_cnt integer;
BEGIN
  -- Check numeric arguments
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_cnt :=  (SELECT count(*)::integer FROM regexp_matches(substr($1, $3), $2, 'pg'));
  RETURN v_cnt;
END;
$$
LANGUAGE plpgsql STRICT;

-- REGEXP_COUNT( string text, pattern text, position int, flags text ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_count(text, text, integer, text)
RETURNS integer
AS $$
DECLARE
  modifiers text;
  v_cnt   integer;
BEGIN
  -- Only modifier can be NULL
  IF $1 IS NULL OR $2 IS NULL OR $3 IS NULL THEN
    RETURN NULL;
  END IF;
  -- Check numeric arguments
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;

  modifiers := oracle.translate_oracle_modifiers($4, true);
  v_cnt := (SELECT count(*)::integer FROM regexp_matches(substr($1, $3), $2, modifiers));
  RETURN v_cnt;
END;
$$
LANGUAGE plpgsql;

--  REGEXP_INSTR( string text, pattern text ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text)
RETURNS integer
AS $$
DECLARE
  v_pos integer;
  v_pattern text;
BEGIN
  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_pos := (SELECT position((SELECT (regexp_matches($1, v_pattern, 'pg'))[1] OFFSET 0 LIMIT 1) IN $1));

  -- position() returns NULL when not found, we need to return 0 instead
  IF v_pos IS NOT NULL THEN
    RETURN v_pos;
  END IF;
  RETURN 0;
END;
$$
LANGUAGE plpgsql STRICT;

--  REGEXP_INSTR( string text, pattern text, position int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer)
RETURNS integer
AS $$
DECLARE
  v_pos integer;
  v_pattern text;
BEGIN
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_pos := (SELECT position((SELECT (regexp_matches(substr($1, $3), v_pattern, 'pg'))[1] OFFSET 0 LIMIT 1) IN $1));

  -- position() returns NULL when not found, we need to return 0 instead
  IF v_pos IS NOT NULL THEN
    RETURN v_pos;
  END IF;
  RETURN 0;
END;
$$
LANGUAGE plpgsql STRICT;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer)
RETURNS integer
AS $$
DECLARE
  v_pos integer;
  v_pattern text;
BEGIN
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''occurence'' must be a number greater than 0';
  END IF;

  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_pos := (SELECT position((SELECT (regexp_matches(substr($1, $3), v_pattern, 'pg'))[1] OFFSET $4 - 1 LIMIT 1) IN $1));

  -- position() returns NULL when not found, we need to return 0 instead
  IF v_pos IS NOT NULL THEN
    RETURN v_pos;
  END IF;
  RETURN 0;
END;
$$
LANGUAGE plpgsql STRICT;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int, return_opt int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer, integer)
RETURNS integer
AS $$
DECLARE
  v_pos integer;
  v_len integer;
  v_pattern text;
BEGIN
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''occurence'' must be a number greater than 0';
  END IF;
  IF $5 != 0 AND $5 != 1 THEN
    RAISE EXCEPTION 'argument ''return_opt'' must be 0 or 1';
  END IF;

  -- Without subexpression specified, assume 0 which mean that the first
  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_pos := (SELECT position((SELECT (regexp_matches(substr($1, $3), v_pattern, 'pg'))[1] OFFSET $4-1 LIMIT 1) IN $1));

  -- position() returns NULL when not found, we need to return 0 instead
  IF v_pos IS NOT NULL THEN
    IF $5 = 1 THEN
      v_len := (SELECT length((SELECT (regexp_matches(substr($1, $3), v_pattern, 'pg'))[1] OFFSET $4 - 1 LIMIT 1)));
      v_pos := v_pos + v_len;
    END IF;
    RETURN v_pos;
  END IF;
  RETURN 0;
END;
$$
LANGUAGE plpgsql STRICT;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int, return_opt int, flags text ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer, integer, text)
RETURNS integer
AS $$
DECLARE
  v_pos integer;
  v_len integer;
  modifiers text;
  v_pattern text;
BEGIN
  -- Only modifier can be NULL
  IF $1 IS NULL OR $2 IS NULL OR $3 IS NULL OR $4 IS NULL OR $5 IS NULL THEN
    RETURN NULL;
  END IF;
  -- Check numeric arguments
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $4 < 1 THEN
      RAISE EXCEPTION 'argument ''occurence'' must be a number greater than 0';
  END IF;
  IF $5 != 0 AND $5 != 1 THEN
    RAISE EXCEPTION 'argument ''return_opt'' must be 0 or 1';
  END IF;
  modifiers := oracle.translate_oracle_modifiers($6, true);

  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';
  v_pos := (SELECT position((SELECT (regexp_matches(substr($1, $3), v_pattern, modifiers))[1] OFFSET $4 - 1 LIMIT 1) IN $1));

  -- position() returns NULL when not found, we need to return 0 instead
  IF v_pos IS NOT NULL THEN
    IF $5 = 1 THEN
      v_len := (SELECT length((SELECT (regexp_matches(substr($1, $3), v_pattern, modifiers))[1] OFFSET $4-1 LIMIT 1)));
      v_pos := v_pos + v_len;
    END IF;
    RETURN v_pos;
  END IF;
  RETURN 0;
END;
$$
LANGUAGE plpgsql;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int, return_opt int, flags text, group int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer, integer, text, integer)
RETURNS integer
AS $$
DECLARE
  v_pos integer := 0;
  v_pos_orig integer := $3;
  v_len integer := 0;
  modifiers text;
  occurrence integer := $4;
  idx integer := 1;
  v_curr_pos integer := 0;
  v_pattern text;
  v_subexpr integer := $7;
BEGIN
  -- Only modifier can be NULL
  IF $1 IS NULL OR $2 IS NULL OR $3 IS NULL OR $4 IS NULL OR $5 IS NULL OR $7 IS NULL THEN
    RETURN NULL;
  END IF;
  -- Check numeric arguments
  IF $3 < 1 THEN
      RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''occurence'' must be a number greater than 0';
  END IF;
  IF $7 < 0 THEN
    RAISE EXCEPTION 'argument ''group'' must be a positive number';
  END IF;
  IF $5 != 0 AND $5 != 1 THEN
    RAISE EXCEPTION 'argument ''return_opt'' must be 0 or 1';
  END IF;

  -- Translate Oracle regexp modifier into PostgreSQl ones
  modifiers := oracle.translate_oracle_modifiers($6, true);

  -- If subexpression value is 0 we need to enclose the pattern between parentheses.
  IF v_subexpr = 0 THEN
     v_pattern := '(' || $2 || ')';
     v_subexpr := 1;
  ELSE
     v_pattern := $2;
  END IF;

  -- To get position of occurrence > 1 we need a more complex code
  LOOP
    v_curr_pos := v_curr_pos + v_len;
    v_pos := (SELECT position((SELECT (regexp_matches(substr($1, v_pos_orig), '('||$2||')', modifiers))[1] OFFSET 0 LIMIT 1) IN substr($1, v_pos_orig)));
    v_len := (SELECT length((SELECT (regexp_matches(substr($1, v_pos_orig), '('||$2||')', modifiers))[1] OFFSET 0 LIMIT 1)));

    EXIT WHEN v_len IS NULL;

    v_pos_orig := v_pos_orig + v_pos + v_len;
    v_curr_pos := v_curr_pos + v_pos;
    idx := idx + 1;

    EXIT WHEN idx > occurrence;
  END LOOP;

  v_pos := (SELECT position((SELECT (regexp_matches(substr($1, v_curr_pos), v_pattern, modifiers))[v_subexpr] OFFSET 0 LIMIT 1) IN substr($1, v_curr_pos)));
  IF v_pos IS NOT NULL THEN
    IF $5 = 1 THEN
      v_len := (SELECT length((SELECT (regexp_matches(substr($1, v_curr_pos), v_pattern, modifiers))[v_subexpr] OFFSET 0 LIMIT 1)));
      v_pos := v_pos + v_len;
    END IF;
    RETURN v_pos + v_curr_pos - 1;
  END IF;
  RETURN 0;
END;
$$
LANGUAGE plpgsql;

-- REGEXP_SUBSTR( string text, pattern text ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_substr(text, text)
RETURNS text
AS $$
DECLARE
  v_substr text;
  v_pattern text;
BEGIN
  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_substr := (SELECT (regexp_matches($1, v_pattern, 'pg'))[1] OFFSET 0 LIMIT 1);
  RETURN v_substr;
END;
$$
LANGUAGE plpgsql STRICT;

-- REGEXP_SUBSTR( string text, pattern text, position int ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_substr(text, text, int)
RETURNS text
AS $$
DECLARE
  v_substr text;
  v_pattern text;
BEGIN
  -- Check numeric arguments
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;

  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_substr := (SELECT (regexp_matches(substr($1, $3), v_pattern, 'pg'))[1] OFFSET 0 LIMIT 1);
  RETURN v_substr;
END;
$$
LANGUAGE plpgsql STRICT;

-- REGEXP_SUBSTR( string text, pattern text, position int, occurence int ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_substr(text, text, integer, integer)
RETURNS text
AS $$
DECLARE
  v_substr text;
  v_pattern text;
BEGIN
  -- Check numeric arguments
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''occurence'' must be a number greater than 0';
  END IF;

  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_substr := (SELECT (regexp_matches(substr($1, $3), v_pattern, 'pg'))[1] OFFSET $4 - 1 LIMIT 1);
  RETURN v_substr;
END;
$$
LANGUAGE plpgsql STRICT;

-- REGEXP_SUBSTR( string text, pattern text, position int, occurence int, flags text ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_substr(text, text, integer, integer, text)
RETURNS text
AS $$
DECLARE
  v_substr text;
  v_pattern text;
  modifiers text;
BEGIN
  -- Only modifier can be NULL
  IF $1 IS NULL OR $2 IS NULL OR $3 IS NULL OR $4 IS NULL THEN
    RETURN NULL;
  END IF;
  -- Check numeric arguments
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''occurence'' must be a number greater than 0';
  END IF;

  modifiers := oracle.translate_oracle_modifiers($5, true);

  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_substr := (SELECT (regexp_matches(substr($1, $3), v_pattern, modifiers))[1] OFFSET $4 - 1 LIMIT 1);
  RETURN v_substr;
END;
$$
LANGUAGE plpgsql;

-- REGEXP_SUBSTR( string text, pattern text, position int, occurence int, flags text, group int ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_substr(text, text, integer, integer, text, int)
RETURNS text
AS $$
DECLARE
  v_substr text;
  v_pattern text;
  modifiers text;
  v_subexpr integer := $6;
  has_group integer;
BEGIN
  -- Only modifier can be NULL
  IF $1 IS NULL OR $2 IS NULL OR $3 IS NULL OR $4 IS NULL OR $6 IS NULL THEN
    RETURN NULL;
  END IF;
  -- Check numeric arguments
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''occurence'' must be a number greater than 0';
  END IF;
  IF v_subexpr < 0 THEN
    RAISE EXCEPTION 'argument ''group'' must be a positive number';
  END IF;

  -- Check that with v_subexpr = 1 we have a capture group otherwise return NULL
  has_group := (SELECT count(*) FROM regexp_matches($2, '(?:[^\\]|^)\(', 'g'));
  IF $6 = 1 AND has_group = 0 THEN
    RETURN NULL;
  END IF;

  modifiers := oracle.translate_oracle_modifiers($5, true);

  -- If subexpression value is 0 we need to enclose the pattern between parentheses.
  IF v_subexpr = 0 THEN
    v_pattern := '(' || $2 || ')';
    v_subexpr := 1;
  ELSE
    v_pattern := $2;
  END IF;

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_substr := (SELECT (regexp_matches(substr($1, $3), v_pattern, modifiers))[v_subexpr] OFFSET $4 - 1 LIMIT 1);
  RETURN v_substr;
END;
$$
LANGUAGE plpgsql;

-- REGEXP_REPLACE( string text, pattern text, replace_string text ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text)
RETURNS text
AS $$
DECLARE
  str text;
BEGIN
  IF $2 IS NULL AND $1 IS NOT NULL THEN
    RETURN $1;
  END IF;
  -- Oracle default behavior is to replace all occurence
  -- whereas PostgreSQL only replace the first occurrence
  -- so we need to add 'g' modifier.
  SELECT pg_catalog.regexp_replace($1, $2, $3, 'g') INTO str;
  RETURN str;
END;
$$
LANGUAGE plpgsql;

-- REGEXP_REPLACE( string text, pattern text, replace_string text, position int ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text, integer)
RETURNS text
AS $$
DECLARE
  v_replaced_str text;
  v_before text;
BEGIN
  IF $1 IS NULL OR $3 IS NULL OR $4 IS NULL THEN
    RETURN NULL;
  END IF;
  IF $2 IS NULL THEN
    RETURN $1;
  END IF;
  -- Check numeric arguments
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;

  v_before = substr($1, 1, $4 - 1);

  -- Oracle default behavior is to replace all occurence
  -- whereas PostgreSQL only replace the first occurrence
  -- so we need to add 'g' modifier.
  v_replaced_str := v_before || pg_catalog.regexp_replace(substr($1, $4), $2, $3, 'g');
  RETURN v_replaced_str;
END;
$$
LANGUAGE plpgsql;

-- REGEXP_REPLACE( string text, pattern text, replace_string text, position int, occurence int ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text, integer, integer)
RETURNS text
AS $$
DECLARE
  v_replaced_str text;
  v_pos integer := $4;
  v_before text := '';
  v_nummatch integer;
BEGIN
  IF $1 IS NULL OR $3 IS NULL OR $4 IS NULL OR $5 IS NULL THEN
    RETURN NULL;
  END IF;
  IF $2 IS NULL THEN
    RETURN $1;
  END IF;
  -- Check numeric arguments
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $5 < 0 THEN
    RAISE EXCEPTION 'argument ''occurrence'' must be a positive number';
  END IF;
  -- Check if the occurrence queried exceeds the number of occurrences
  IF $5 > 1 THEN
    v_nummatch := (SELECT count(*) FROM regexp_matches(substr($1, $4), $2, 'g'));
    IF $5 > v_nummatch THEN
      RETURN $1;
    END IF;
    -- Get the position of the occurrence we are looking for
    v_pos := oracle.regexp_instr($1, $2, $4, $5, 0, '', 1);
    IF v_pos = 0 THEN
      RETURN $1;
    END IF;
  END IF;
  -- Get the substring before this position we will need to restore it
  v_before := substr($1, 1, v_pos - 1);

  -- Replace all occurrences
  IF $5 = 0 THEN
    v_replaced_str := v_before || pg_catalog.regexp_replace(substr($1, v_pos), $2, $3, 'g');
  ELSE
    -- Replace the first occurrence
    v_replaced_str := v_before || pg_catalog.regexp_replace(substr($1, v_pos), $2, $3);
  END IF;

  RETURN v_replaced_str;
END;
$$
LANGUAGE plpgsql;

-- REGEXP_REPLACE( string text, pattern text, replace_string text, position int, occurence int, flags text ) -> text
CREATE OR REPLACE FUNCTION oracle.regexp_replace(text, text, text, integer, integer, text)
RETURNS text
AS $$
DECLARE
  v_replaced_str text;
  v_pos integer := $4;
  v_nummatch integer;
  v_before text := '';
  modifiers text := '';
BEGIN
  IF $1 IS NULL OR $3 IS NULL OR $4 IS NULL OR $5 IS NULL THEN
    RETURN NULL;
  END IF;
  IF $2 IS NULL THEN
    RETURN $1;
  END IF;
  -- Check numeric arguments
  IF $4 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;
  IF $5 < 0 THEN
    RAISE EXCEPTION 'argument ''occurrence'' must be a positive number';
  END IF;
  -- Set the modifiers
  IF $5 = 0 THEN
    modifiers := oracle.translate_oracle_modifiers($6, true);
  ELSE
    modifiers := oracle.translate_oracle_modifiers($6, false);
  END IF;
  -- Check if the occurrence queried exceeds the number of occurrences
  IF $5 > 1 THEN
    v_nummatch := (SELECT count(*) FROM regexp_matches(substr($1, $4), $2, $6||'g'));
    IF $5 > v_nummatch THEN
      RETURN $1;
    END IF;
    -- Get the position of the occurrence we are looking for
    v_pos := oracle.regexp_instr($1, $2, $4, $5, 0, $6, 1);
    IF v_pos = 0 THEN
      RETURN $1;
    END IF;
  END IF;
  -- Get the substring before this position we will need to restore it
  v_before := substr($1, 1, v_pos - 1);
  -- Replace occurrence(s)
  v_replaced_str := v_before || pg_catalog.regexp_replace(substr($1, v_pos), $2, $3, modifiers);
  RETURN v_replaced_str;
END;
$$
LANGUAGE plpgsql;

