/*
 * Check function for config table partition types
 */
CREATE FUNCTION check_partition_type (p_type text) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE SECURITY DEFINER
    AS $$
DECLARE
v_result    boolean;
BEGIN
    SELECT p_type IN ('time-static', 'time-dynamic', 'time-custom', 'id-static', 'id-dynamic') INTO v_result;
    RETURN v_result;
END
$$;
