/*
 * Check PostgreSQL version number. Parameter must be full 3 point version.
 * Returns true if current version is greater than or equal to the parameter given.
 */
CREATE FUNCTION check_version(p_check_version text) RETURNS boolean
    LANGUAGE plpgsql STABLE
    AS $$
DECLARE

v_check_version     text[];
v_current_version   text[] := string_to_array(current_setting('server_version'), '.');
 
BEGIN

v_check_version := string_to_array(p_check_version, '.');

IF v_current_version[1]::int > v_check_version[1]::int THEN
    RETURN true;
END IF;
IF v_current_version[1]::int = v_check_version[1]::int THEN
    IF v_current_version[2]::int > v_check_version[2]::int THEN
        RETURN true;
    END IF;
    IF v_current_version[2]::int = v_check_version[2]::int THEN
        IF v_current_version[3]::int >= v_check_version[3]::int THEN
            RETURN true;
        END IF; -- 0.0.x
    END IF; -- 0.x.0
END IF; -- x.0.0

RETURN false;

END
$$;
