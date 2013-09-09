/*
 * Truncate the name of the given object if it is greater than the postgres default max (63 characters).
 * Also appends given suffix and schema if given and truncates the name so that the entire suffix will fit.
 * Returns original name with schema given if it doesn't require truncation
 */
CREATE FUNCTION check_name_length (p_object_name text, p_object_schema text DEFAULT NULL, p_suffix text DEFAULT NULL, p_table_partition boolean DEFAULT FALSE) RETURNS text
    LANGUAGE plpgsql SECURITY DEFINER
    AS $$
DECLARE
    v_new_length    int;
    v_new_name      text;
BEGIN

IF p_table_partition IS TRUE AND (p_suffix IS NULL OR p_object_schema IS NULL) THEN
    RAISE EXCEPTION 'Table partition name requires a schema and suffix value';
END IF;

IF p_table_partition THEN  -- 61 characters to account for _p in partition name
    IF char_length(p_object_name) + char_length(p_suffix) >= 61 THEN
        v_new_length := 61 - char_length(p_suffix);
        v_new_name := p_object_schema ||'.'|| substring(p_object_name from 1 for v_new_length) || '_p' || p_suffix; 
    ELSE
        v_new_name := p_object_schema ||'.'||p_object_name||'_p'||p_suffix;
    END IF;
ELSE
    IF char_length(p_object_name) + char_length(COALESCE(p_suffix, '')) >= 63 THEN
        v_new_length := 63 - char_length(COALESCE(p_suffix, ''));
        v_new_name := COALESCE(p_object_schema ||'.', '') || substring(p_object_name from 1 for v_new_length) || COALESCE(p_suffix, ''); 
    ELSE
        v_new_name := COALESCE(p_object_schema ||'.', '') || p_object_name||COALESCE(p_suffix, '');
    END IF;
END IF;

RETURN v_new_name;

END
$$;

