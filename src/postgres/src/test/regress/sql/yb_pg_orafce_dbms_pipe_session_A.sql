\set ECHO none
SET client_min_messages = warning;
DROP TABLE IF EXISTS TEMP;
CREATE TABLE TEMP(id integer,name text);
INSERT INTO TEMP VALUES (1,'bob'),(2,'rob'),(3,'john');

DROP USER IF EXISTS pipe_test_owner;
CREATE ROLE pipe_test_owner WITH CREATEROLE;
ALTER TABLE TEMP OWNER TO pipe_test_owner;
SET client_min_messages = notice;

-- Notify session B of 'pipe_test_owner' having been created.
SELECT dbms_pipe.pack_message(1);
SELECT dbms_pipe.send_message('pipe_test_owner_created_notifier');
-- Create a new connection under the userid of pipe_test_owner
SET SESSION AUTHORIZATION pipe_test_owner;

/* create an implicit pipe and sends message using
 * send_message(text,integer,integer)
 */
CREATE OR REPLACE FUNCTION send(pipename text) RETURNS void AS $$
BEGIN
        IF dbms_pipe.send_message(pipename,2,10) = 1 THEN
                RAISE NOTICE 'Timeout';
                PERFORM pg_sleep(2);
                PERFORM dbms_pipe.send_message(pipename,2,10);
        END IF;
END; $$ LANGUAGE plpgsql;

-- Test pack_message for all supported types and send_message
CREATE OR REPLACE FUNCTION createImplicitPipe() RETURNS void AS $$
DECLARE
        row TEMP%ROWTYPE;
BEGIN
        PERFORM dbms_pipe.pack_message('Message From Session A'::text);
        PERFORM send('named_pipe');
        PERFORM dbms_pipe.pack_message('2013-01-01'::date);
        PERFORM send('named_pipe');
        PERFORM dbms_pipe.pack_message('2013-01-01 09:00:00'::timestamp);
        PERFORM send('named_pipe');
        PERFORM dbms_pipe.pack_message('2013-01-01 09:00:00-08'::timestamptz);
        PERFORM send('named_pipe');
        PERFORM dbms_pipe.pack_message(12345.6789::numeric);
        PERFORM send('named_pipe');
        PERFORM dbms_pipe.pack_message(12345::integer);
        PERFORM send('named_pipe');
        PERFORM dbms_pipe.pack_message(99999999999::bigint);
        PERFORM send('named_pipe');
        PERFORM dbms_pipe.pack_message(E'\\201'::bytea);
        PERFORM send('named_pipe');
        SELECT * INTO row FROM TEMP WHERE id=2;
	PERFORM dbms_pipe.pack_message(row);
        PERFORM send('named_pipe');
END; $$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION bulkSend() RETURNS void AS $$
DECLARE
        row TEMP%ROWTYPE;
BEGIN
        PERFORM dbms_pipe.pack_message('Message From Session A'::text);
        PERFORM dbms_pipe.pack_message('2013-01-01'::date);
        PERFORM dbms_pipe.pack_message('2013-01-01 09:00:00'::timestamp);
        PERFORM dbms_pipe.pack_message('2013-01-01 09:00:00-08'::timestamptz);
        PERFORM dbms_pipe.pack_message(12345.6789::numeric);
        PERFORM dbms_pipe.pack_message(12345::integer);
        PERFORM dbms_pipe.pack_message(99999999999::bigint);
        PERFORM dbms_pipe.pack_message(E'\\201'::bytea);
        SELECT * INTO row FROM TEMP WHERE id=2;
        PERFORM dbms_pipe.pack_message(row);
        PERFORM send('named_pipe_2');
END; $$ LANGUAGE plpgsql;


/* Creates an explicit pipe using either create_pipe(text,integer,bool),
 * create_pipe(text,integer) OR create_pipe(text).
 * In case third parameter (bool) absent, default is false, that is, it's a public pipe.
 */
CREATE OR REPLACE FUNCTION createPipe(name text,ver integer) RETURNS void AS $$
BEGIN
	IF ver = 3 THEN
                PERFORM dbms_pipe.create_pipe(name,4,true);
        ELSIF ver = 2 THEN
                PERFORM dbms_pipe.create_pipe(name,4);
        ELSE
                PERFORM dbms_pipe.create_pipe(name);
	END IF;
END; $$ LANGUAGE plpgsql;


/* Testing create_pipe for different versions, one of them, is the case of
 * private pipe
 */

CREATE OR REPLACE FUNCTION createExplicitPipe(pipename text,create_version integer) RETURNS void AS $$
DECLARE
        row TEMP%ROWTYPE;
BEGIN

	PERFORM createPipe(pipename,create_version);

	PERFORM dbms_pipe.reset_buffer();

        PERFORM dbms_pipe.pack_message('Message From Session A'::text);
        PERFORM send(pipename);
        PERFORM dbms_pipe.pack_message('2013-01-01'::date);
        PERFORM send(pipename);
        PERFORM dbms_pipe.pack_message('2013-01-01 09:00:00'::timestamp);
        PERFORM send(pipename);
        PERFORM dbms_pipe.pack_message('2013-01-01 09:00:00-08'::timestamptz);
        PERFORM send(pipename);
        PERFORM dbms_pipe.pack_message(12345.6789::numeric);
        PERFORM send(pipename);
        PERFORM dbms_pipe.pack_message(12345::integer);
        PERFORM send(pipename);
        PERFORM dbms_pipe.pack_message(99999999999::bigint);
        PERFORM send(pipename);
        PERFORM dbms_pipe.pack_message(E'\\201'::bytea);
        PERFORM send(pipename);
        SELECT * INTO row FROM TEMP WHERE id=2;
        PERFORM dbms_pipe.pack_message(row);
        PERFORM send(pipename);
END; $$ LANGUAGE plpgsql;


-- Test send_message(text)
CREATE OR REPLACE FUNCTION checkSend1() RETURNS void AS $$
BEGIN
	PERFORM dbms_pipe.pack_message('checking one-argument send_message()');
	PERFORM dbms_pipe.send_message('pipe_name_1');
END; $$ LANGUAGE plpgsql;


-- Test send_message(text,integer)
CREATE OR REPLACE FUNCTION checkSend2() RETURNS void AS $$
BEGIN
        PERFORM dbms_pipe.pack_message('checking two-argument send_message()');
	IF dbms_pipe.send_message('pipe_name_2',2) = 1 THEN
                RAISE NOTICE 'Timeout';
                PERFORM pg_sleep(2);
                PERFORM dbms_pipe.send_message('pipe_name_2',2);
        END IF;
END; $$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION notifyDropTemp() RETURNS void AS $$
BEGIN
	PERFORM dbms_pipe.pack_message(1);
	PERFORM dbms_pipe.send_message('pipe_name_3');
END; $$ LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION checkUniqueSessionNameA() RETURNS void AS $$
BEGIN
	PERFORM dbms_pipe.pack_message(dbms_pipe.unique_session_name());
	PERFORM dbms_pipe.send_message('pipe_name_4');
END; $$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION notify(pipename text) RETURNS void AS $$
BEGIN
	PERFORM dbms_pipe.pack_message(1);
	PERFORM dbms_pipe.send_message(pipename);
END; $$ LANGUAGE plpgsql;

\set ECHO all

SELECT createImplicitPipe();

-- Bulk send messages
SELECT bulkSend();

-- An explicit private pipe
SELECT notify('recv_private1_notifier');
SELECT createExplicitPipe('private_pipe_1',3);

-- An explicit private pipe
SELECT notify('recv_private2_notifier');
SELECT createExplicitPipe('private_pipe_2',3);

-- An explicit public pipe (uses two-argument create_pipe)
SELECT notify('recv_public1_notifier');
SELECT createExplicitPipe('public_pipe_3',2);

-- An explicit public pipe (uses one-argument create_pipe)
SELECT notify('recv_public2_notifier');
SELECT createExplicitPipe('public_pipe_4',1);

-- tests send_message(text)
SELECT checkSend1();

-- tests send_message(text,integer)
SELECT checkSend2();

SELECT notifyDropTemp();

-- tests unique_session_name()
SELECT checkUniqueSessionNameA();

DROP FUNCTION createImplicitPipe();
DROP FUNCTION createExplicitPipe(text,integer);
DROP FUNCTION createPipe(text,integer);
DROP FUNCTION checkSend1();
DROP FUNCTION checkSend2();
DROP FUNCTION checkUniqueSessionNameA();
DROP FUNCTION bulkSend();
DROP FUNCTION notifyDropTemp();
DROP FUNCTION notify(text);
DROP FUNCTION send(text);
