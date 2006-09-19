CREATE TYPE testt AS (x integer, y integer, v varchar);

CREATE OR REPLACE FUNCTION st(integer, integer, varchar) 
RETURNS void AS $$
DECLARE t testt; r record;
BEGIN t.x := $1; t.y := $2; t.v := $3;
  select into r 10,10,'boo';
  PERFORM dbms_pipe.pack_message(t);
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION sk() 
RETURNS void AS $$
DECLARE t testt;
 o testt;
BEGIN t.x := 1; t.y := 2; t.v := 'Pavel Stehule';
  RAISE NOTICE 'SEND';
  PERFORM dbms_pipe.pack_message(t);
  PERFORM dbms_pipe.send_message('boo',4,10);
  RAISE NOTICE 'RECEIVE';
--  PERFORM dbms_pipe.receive_message('boo',4);
--  SELECT INTO o * from dbms_pipe.unpack_message_record() as (x integer, y integer, v varchar);
--  RAISE NOTICE 'received %', o.v;  
END;
$$ LANGUAGE plpgsql;



CREATE OR REPLACE FUNCTION SessionA() RETURNS void AS $$
BEGIN
  FOR i IN 1..100000 LOOP
    PERFORM dbms_pipe.pack_message('Prvni '||i);
    PERFORM dbms_pipe.pack_message('Druhy '||i);
    RAISE NOTICE 'SEND';
    IF dbms_pipe.send_message('pipe_name',4,10) = 1 THEN
      RAISE NOTICE 'Timeout';
      PERFORM pg_sleep(5);
      PERFORM dbms_pipe.send_message('pipe_name',4,10);
    END IF;
    PERFORM pg_sleep(random());
  END LOOP;
END; $$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION SessionB() RETURNS void AS $$
BEGIN
  FOR i IN 1..100000 LOOP
    IF dbms_pipe.receive_message('pipe_name',4) = 1 THEN
      RAISE NOTICE 'Timeout';
      PERFORM pg_sleep(5);
      CONTINUE;
    END IF;
    RAISE NOTICE 'RECEIVE % %', dbms_pipe.unpack_message_text(), 
      dbms_pipe.unpack_message_text();
    PERFORM pg_sleep(random());
  END LOOP;
END; $$ LANGUAGE plpgsql;


