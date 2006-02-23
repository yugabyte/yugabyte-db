CREATE OR REPLACE FUNCTION SessionA() RETURNS void AS $$
BEGIN
  FOR i IN 1..1000 LOOP
    PERFORM dbms_pipe.pack_message('Prvni '||i);
    PERFORM dbms_pipe.pack_message('Druhy '||i);
    RAISE NOTICE 'SEND';
    IF dbms_pipe.send_message('pipe_name',4) = 1 THEN
      RAISE NOTICE 'Timeout';
      PERFORM pg_sleep(5);
      PERFORM dbms_pipe.send_message('pipe_name',4);
    END IF;
    PERFORM pg_sleep(random());
  END LOOP;
END; $$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION SessionB() RETURNS void AS $$
BEGIN
  FOR i IN 1..1000 LOOP
    IF dbms_pipe.receive_message('pipe_name',4) = 1 THEN
      RAISE NOTICE 'Timeout';
      PERFORM pg_sleep(5);
      CONTINUE;
    END IF;
    RAISE NOTICE 'RECEIVE % %', dbms_pipe.unpack_message(), 
      dbms_pipe.unpack_message();
    PERFORM pg_sleep(random());
  END LOOP;
END; $$ LANGUAGE plpgsql;
