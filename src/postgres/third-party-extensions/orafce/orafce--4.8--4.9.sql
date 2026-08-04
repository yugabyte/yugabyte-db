-- regexp_count_pattern_fix: replace any occurence of a dot into a [^\n] pattern.
CREATE OR REPLACE FUNCTION oracle.regexp_count_pattern_fix(text)
RETURNS text
AS $$
DECLARE
  v_pattern text;
BEGIN
  -- Replace any occurences of a dot by [^\n]
  -- to have the same behavior as Oracle
  v_pattern := regexp_replace($1, '\\\.', '#ESCDOT#', 'g');
  v_pattern := regexp_replace(v_pattern, '\.', '[^\n]', 'g');
  v_pattern := regexp_replace(v_pattern, '#ESCDOT#', '\.', 'g');

  RETURN v_pattern;
END;
$$
LANGUAGE plpgsql IMMUTABLE STRICT PARALLEL SAFE;

-- REGEXP_COUNT( string text, pattern text ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_count(text, text)
RETURNS integer
AS $$
  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  SELECT count(*)::integer FROM regexp_matches($1, oracle.regexp_count_pattern_fix($2), 'sg');
$$
LANGUAGE 'sql' STRICT IMMUTABLE PARALLEL SAFE;

-- REGEXP_COUNT( string text, pattern text, position int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_count(text, text, integer)
RETURNS integer
AS $$
DECLARE
  v_cnt integer;
  v_pattern text;
BEGIN
  -- Check numeric arguments
  IF $3 < 1 THEN
    RAISE EXCEPTION 'argument ''position'' must be a number greater than 0';
  END IF;

  v_pattern := '(' || oracle.regexp_count_pattern_fix($2) || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 's' modifier to affect
  -- newline-sensitivity but not ^ and $ search.
  v_cnt :=  (SELECT count(*)::integer FROM regexp_matches(substr($1, $3), v_pattern, 'sg'));
  RETURN v_cnt;
END;
$$
LANGUAGE plpgsql STRICT IMMUTABLE PARALLEL SAFE;

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
  v_cnt :=  (SELECT count(*)::integer FROM regexp_matches(substr($1, $3), oracle.regexp_count_pattern_fix($2), 'sg'));
  RETURN v_cnt;
END;
$$
LANGUAGE plpgsql STRICT IMMUTABLE PARALLEL SAFE;

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
LANGUAGE plpgsql IMMUTABLE PARALLEL SAFE;

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
  v_substr := (SELECT (regexp_matches($1, v_pattern, 'sg'))[1] OFFSET 0 LIMIT 1);
  RETURN v_substr;
END;
$$
LANGUAGE plpgsql STRICT IMMUTABLE PARALLEL SAFE;

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
  v_substr := (SELECT (regexp_matches(substr($1, $3), v_pattern, 'sg'))[1] OFFSET 0 LIMIT 1);
  RETURN v_substr;
END;
$$
LANGUAGE plpgsql STRICT IMMUTABLE PARALLEL SAFE;

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
  v_substr := (SELECT (regexp_matches(substr($1, $3), v_pattern, 'sg'))[1] OFFSET $4 - 1 LIMIT 1);
  RETURN v_substr;
END;
$$
LANGUAGE plpgsql STRICT IMMUTABLE PARALLEL SAFE;

