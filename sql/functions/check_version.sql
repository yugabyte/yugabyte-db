CREATE FUNCTION check_version(p_check_version text) RETURNS boolean
    LANGUAGE plpgsql STABLE
    AS $$
DECLARE

v_check_version     text[];
v_current_version   text[] := string_to_array(current_setting('server_version'), '.');
 
BEGIN
/*
 * Check PostgreSQL version number. Parameter must be full 3 point version if prior to 10.0. Otherwise 2 point version.
 * Returns true if current version is greater than or equal to the parameter given.
 * If running version is devel, alpha, beta or rc, the check always returns true.
 */

v_check_version := string_to_array(p_check_version, '.');

IF substring(v_current_version[1] from 'devel') IS NOT NULL THEN
    -- You're running a test version. You're on your own if things fail.
    RETURN true;
END IF;
IF v_current_version[1]::int > v_check_version[1]::int THEN
    RETURN true;
END IF;
IF v_current_version[1]::int = v_check_version[1]::int THEN
    IF substring(v_current_version[2] from 'beta') IS NOT NULL 
        OR substring(v_current_version[2] from 'alpha') IS NOT NULL 
        OR substring(v_current_version[2] from 'rc') IS NOT NULL 
    THEN
        -- You're running a test version. You're on your own if things fail.
        RETURN true;
    END IF;
    IF v_current_version[2]::int > v_check_version[2]::int THEN
        RETURN true;
    END IF;
    IF v_current_version[2]::int = v_check_version[2]::int THEN
        IF array_length(v_current_version, 1) <= 2 THEN
            -- Account for reduction to 2 number version in 10.0
            RETURN true;
        END IF; 
        IF v_current_version[3]::int >= v_check_version[3]::int THEN
            RETURN true;
        END IF; -- 0.0.x
    END IF; -- 0.x.0
END IF; -- x.0.0

RETURN false;

END
$$;

