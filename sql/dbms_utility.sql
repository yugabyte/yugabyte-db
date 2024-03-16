\set ECHO none
\pset format unaligned
/*
 * Test for dbms_utility.format_call_stack(char mode). 
 * Mode is hex. 
 * The callstack returned is passed to regex_replace function.
 * Regex_replace replaces the function oid from the stack with zero.
 * This is done to avoid random results due to different oids generated.
 * Also the line number and () of the function is removed since it is different
 * across different pg version.
 */

CREATE OR REPLACE FUNCTION checkHexCallStack() returns text  as $$
        DECLARE
             stack text;
        BEGIN
             select * INTO stack from dbms_utility.format_call_stack('o');
             select * INTO stack from regexp_replace(stack,'[ 0-9a-fA-F]{4}[0-9a-fA-F]{4}','       0','g');
             select * INTO stack from regexp_replace(stack,'[45()]','','g');
             return stack;
        END;
$$ LANGUAGE plpgsql;

/*
 * Test for dbms_utility.format_call_stack(char mode). 
 * Mode is integer.
 */

CREATE OR REPLACE FUNCTION checkIntCallStack() returns text  as $$
        DECLARE
             stack text;
        BEGIN
             select * INTO stack from dbms_utility.format_call_stack('p');
             select * INTO stack from regexp_replace(stack,'[ 0-9]{3}[0-9]{5}','       0','g');
             select * INTO stack from regexp_replace(stack,'[45()]','','g');
             return stack;
        END;
$$ LANGUAGE plpgsql;

/*
 * Test for dbms_utility.format_call_stack(char mode). 
 * Mode is integer with unpadded output.
 */

CREATE OR REPLACE FUNCTION checkIntUnpaddedCallStack() returns text  as $$
        DECLARE
             stack text;
        BEGIN
             select * INTO stack from dbms_utility.format_call_stack('s');
             select * INTO stack from regexp_replace(stack,'[0-9]{5,}','0','g');
             select * INTO stack from regexp_replace(stack,'[45()]','','g');
             return stack;
        END;
$$ LANGUAGE plpgsql;

select * from checkHexCallStack();
select * from checkIntCallStack();
select * from checkIntUnpaddedCallStack();

DROP FUNCTION checkHexCallStack();
DROP FUNCTION checkIntCallStack();
DROP FUNCTION checkIntUnpaddedCallStack();

/*
 * Test for dbms_utility.get_time(), the result is rounded
 * to have constant result in the regression test.
 */
DO $$
DECLARE
    start_time integer;
    end_time integer;
BEGIN
    start_time := DBMS_UTILITY.GET_TIME();
    PERFORM pg_sleep(2);
    end_time := DBMS_UTILITY.GET_TIME();
    -- clamp long runtime on slow build machines to the 2s the testsuite is expecting
    IF end_time BETWEEN start_time + 300 AND start_time + 1000 THEN end_time := start_time + 250; END IF;
    RAISE NOTICE 'Execution time: % seconds', trunc((end_time - start_time)::numeric/100);
END
$$;


