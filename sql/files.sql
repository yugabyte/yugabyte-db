\set ECHO none
SET client_min_messages = NOTICE;
\set ECHO all

INSERT INTO utl_file.utl_file_dir(dir) VALUES(utl_file.tmpdir());

CREATE OR REPLACE FUNCTION gen_file(dir text) RETURNS void AS $$
DECLARE
  f utl_file.file_type;
BEGIN
  f := utl_file.fopen(dir, 'regress_orafce.txt', 'w');
  PERFORM utl_file.put_line(f, 'ABC');
  PERFORM utl_file.put_line(f, '123'::numeric);
  PERFORM utl_file.put_line(f, '-----');
  PERFORM utl_file.new_line(f);
  PERFORM utl_file.put_line(f, '-----');
  PERFORM utl_file.new_line(f, 0);
  PERFORM utl_file.put_line(f, '-----');
  PERFORM utl_file.new_line(f, 2);
  PERFORM utl_file.put_line(f, '-----');
  PERFORM utl_file.put(f, 'A');
  PERFORM utl_file.put(f, 'B');
  PERFORM utl_file.new_line(f);
  PERFORM utl_file.putf(f, '[1=%s, 2=%s, 3=%s, 4=%s, 5=%s]', '1', '2', '3', '4', '5');
  PERFORM utl_file.new_line(f);
  PERFORM utl_file.put_line(f, '1234567890');
  f := utl_file.fclose(f);
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION read_file(dir text) RETURNS void AS $$
DECLARE
  f utl_file.file_type;
BEGIN
  f := utl_file.fopen(dir, 'regress_orafce.txt', 'r');
  FOR i IN 1..11 LOOP
    RAISE NOTICE '[%] >>%<<', i, utl_file.get_line(f);
  END LOOP;
  RAISE NOTICE '>>%<<', utl_file.get_line(f, 4);
  RAISE NOTICE '>>%<<', utl_file.get_line(f, 4);
  RAISE NOTICE '>>%<<', utl_file.get_line(f);
  RAISE NOTICE '>>%<<', utl_file.get_line(f);
  EXCEPTION
    -- WHEN no_data_found THEN,  8.1 plpgsql doesn't know no_data_found
    WHEN others THEN
      RAISE NOTICE 'finish % ', sqlerrm;
      RAISE NOTICE 'is_open = %', utl_file.is_open(f);
      PERFORM utl_file.fclose_all();
      RAISE NOTICE 'is_open = %', utl_file.is_open(f);
END;
$$ LANGUAGE plpgsql;

SELECT gen_file(utl_file.tmpdir());
SELECT fexists FROM utl_file.fgetattr(utl_file.tmpdir(), 'regress_orafce.txt');
SELECT utl_file.fcopy(utl_file.tmpdir(), 'regress_orafce.txt', utl_file.tmpdir(), 'regress_orafce2.txt');
SELECT fexists FROM utl_file.fgetattr(utl_file.tmpdir(), 'regress_orafce2.txt');
SELECT utl_file.frename(utl_file.tmpdir(), 'regress_orafce2.txt', utl_file.tmpdir(), 'regress_orafce.txt', true);
SELECT fexists FROM utl_file.fgetattr(utl_file.tmpdir(), 'regress_orafce.txt');
SELECT fexists FROM utl_file.fgetattr(utl_file.tmpdir(), 'regress_orafce2.txt');
SELECT read_file(utl_file.tmpdir());
SELECT utl_file.fremove(utl_file.tmpdir(), 'regress_orafce.txt');
SELECT fexists FROM utl_file.fgetattr(utl_file.tmpdir(), 'regress_orafce.txt');
DROP FUNCTION gen_file(text);
DROP FUNCTION read_file(text);

DELETE FROM utl_file.utl_file_dir;
