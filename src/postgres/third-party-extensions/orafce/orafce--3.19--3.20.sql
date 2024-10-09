--  REGEXP_INSTR( string text, pattern text, position int, occurence int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer)
 RETURNS integer
 LANGUAGE plpgsql
 STRICT
AS $function$
DECLARE
  v_pos integer;
  v_pattern text;
  r record;
  start_pos integer DEFAULT 1;
  new_start integer;
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

  $1 := substr($1, $3);
  start_pos := $3;

  FOR r IN SELECT (regexp_matches($1, v_pattern, 'pg'))[1]
  LOOP
    v_pos := position(r.regexp_matches IN $1);

    IF $4 = 1 THEN
      RETURN v_pos + start_pos - 1;
    ELSE
      $4 := $4 - 1;
    END IF;

    new_start := v_pos + length(r.regexp_matches);
    $1 := substr($1, new_start);
    start_pos := start_pos + new_start - 1;
  END LOOP;

  RETURN 0;
END;
$function$;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int, return_opt int ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer, integer)
 RETURNS integer
 LANGUAGE plpgsql
 STRICT
AS $function$
DECLARE
  v_pos integer;
  v_pattern text;
  r record;
  start_pos integer DEFAULT 1;
  new_start integer;
  pattern_match_len integer;
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
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.

  $1 := substr($1, $3);
  start_pos := $3;

  FOR r IN SELECT (regexp_matches($1, v_pattern, 'pg'))[1]
  LOOP
    v_pos := position(r.regexp_matches IN $1);

    pattern_match_len = length(r.regexp_matches);

    IF $4 = 1 THEN
      IF $5 = 1 THEN
        new_start := v_pos + pattern_match_len;
        start_pos := start_pos + new_start - 1;
        RETURN start_pos;
      END IF;
      RETURN v_pos + start_pos - 1;
    ELSE
      $4 := $4 - 1;
    END IF;

    new_start := v_pos + pattern_match_len;
    $1 := substr($1, new_start);
    start_pos := start_pos + new_start - 1;
  END LOOP;

  RETURN 0;
END;
$function$;

--  REGEXP_INSTR( string text, pattern text, position int, occurence int, return_opt int, flags text ) -> integer
CREATE OR REPLACE FUNCTION oracle.regexp_instr(text, text, integer, integer, integer, text)
 RETURNS integer
 LANGUAGE plpgsql
AS $function$
DECLARE
  v_pos integer;
  v_pattern text;
  r record;
  start_pos integer DEFAULT 1;
  new_start integer;
  pattern_match_len integer;
  modifiers text;
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

  -- Translate Oracle regexp modifier into PostgreSQL ones
  IF $6 IS NOT NULL THEN
    modifiers := oracle.translate_oracle_modifiers($6, true);
  ELSE
    modifiers := 'pg';
  END IF;

  -- Without subexpression specified, assume 0 which mean that the first
  -- position for the substring matching the whole pattern is returned.
  -- We need to enclose the pattern between parentheses.
  v_pattern := '(' || $2 || ')';

  -- Oracle default behavior is newline-sensitive,
  -- PostgreSQL not, so force 'p' modifier to affect
  -- newline-sensitivity but not ^ and $ search.

  $1 := substr($1, $3);
  start_pos := $3;

  FOR r IN SELECT (regexp_matches($1, v_pattern, modifiers))[1]
  LOOP
    v_pos := position(r.regexp_matches IN $1);

    pattern_match_len = length(r.regexp_matches);

    IF $4 = 1 THEN
      IF $5 = 1 THEN
        new_start := v_pos + pattern_match_len;
        start_pos := start_pos + new_start - 1;
        RETURN start_pos;
      END IF;
      RETURN v_pos + start_pos - 1;
    ELSE
      $4 := $4 - 1;
    END IF;

    new_start := v_pos + pattern_match_len;
    $1 := substr($1, new_start);
    start_pos := start_pos + new_start - 1;
  END LOOP;

  RETURN 0;
END;
$function$;

