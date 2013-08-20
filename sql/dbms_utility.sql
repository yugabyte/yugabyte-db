\set ECHO none
/*
 * Test for dbms_utility.format_call_stack(char mode). 
 * Mode is hex. 
 * The callstack returned is passed to regex_replace function.
 * Regex_replace replaces the function oid from the stack with a blank.
 * This is done to avoid random results due to different oids generated.
 */

CREATE OR REPLACE FUNCTION checkHexCallStack() returns text  as $$
        DECLARE
             stack text;
        BEGIN
             select * INTO stack from dbms_utility.format_call_stack('o');
             select * INTO stack from regexp_replace(stack,'[ 0-9a-fA-F]{4}[0-9a-fA-F]{4}','','g');
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
             select * INTO stack from regexp_replace(stack,'[ 0-9]{3}[0-9]{5}','','g');
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
             select * INTO stack from regexp_replace(stack,'[0-9]{5,}','','g');
             return stack;
        END;
$$ LANGUAGE plpgsql;

select * from checkHexCallStack();
select * from checkIntCallStack();
select * from checkIntUnpaddedCallStack();

DROP FUNCTION checkHexCallStack();
DROP FUNCTION checkIntCallStack();
DROP FUNCTION checkIntUnpaddedCallStack();

