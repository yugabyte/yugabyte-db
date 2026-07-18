SET client_min_messages = NOTICE;
\set VERBOSITY terse
\set ECHO all
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

/* Test functions utl_file.fflush(utl_file.file_type) and 
 * utl_file.get_nextline(utl_file.file_type)
 * This function tests the positive test case of fflush by reading from the 
 * file after flushing the contents to the file. 
 */
CREATE OR REPLACE FUNCTION checkFlushFile(dir text) RETURNS void AS $$
DECLARE
  f utl_file.file_type;
  f1 utl_file.file_type;
  ret_val text;
  i integer;
BEGIN
  f := utl_file.fopen(dir, 'regressflush_orafce.txt', 'a');
  PERFORM utl_file.put_line(f, 'ABC');
  PERFORM utl_file.new_line(f);
  PERFORM utl_file.put_line(f, '123'::numeric);
  PERFORM utl_file.new_line(f);
  PERFORM utl_file.putf(f, '[1=%s, 2=%s, 3=%s, 4=%s, 5=%s]', '1', '2', '3', '4', '5');
  PERFORM utl_file.fflush(f);
  f1 := utl_file.fopen(dir, 'regressflush_orafce.txt', 'r');
  ret_val=utl_file.get_nextline(f1);
  i:=1;
  WHILE ret_val IS NOT NULL LOOP
    RAISE NOTICE '[%] >>%<<', i,ret_val;
    ret_val := utl_file.get_nextline(f1);
    i:=i+1;
  END LOOP;
  RAISE NOTICE '>>%<<', ret_val;
  f1 := utl_file.fclose(f1);
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

SELECT EXISTS(SELECT * FROM pg_catalog.pg_class where relname='utl_file_dir') AS exists;

SELECT EXISTS(SELECT * FROM pg_catalog.pg_type where typname='file_type') AS exists;

-- Trying to access a file in path not registered
SELECT utl_file.fopen(utl_file.tmpdir(),'sample.txt','r');

-- Trying to access file in a non-existent directory
INSERT INTO utl_file.utl_file_dir(dir) VALUES('test_tmp_dir');
SELECT utl_file.fopen('test_tmp_dir','file.txt.','w');
DELETE FROM utl_file.utl_file_dir WHERE dir LIKE 'test_tmp_dir';
-- Add tmpdir() to utl_file_dir table
INSERT INTO utl_file.utl_file_dir(dir) VALUES(utl_file.tmpdir());

SELECT count(*) from utl_file.utl_file_dir where dir <> '';

-- Trying to access non-existent file
SELECT utl_file.fopen(utl_file.tmpdir(),'non_existent_file.txt','r');

--Other test cases
--run this under unprivileged user
CREATE ROLE test_role_files LOGIN;
SET ROLE TO test_role_files;

-- should to fail, unpriviliged user cannot to change utl_file_dir
INSERT INTO utl_file.utl_file_dir(dir) VALUES('test_tmp_dir');

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
SELECT checkFlushFile(utl_file.tmpdir());
SELECT utl_file.fremove(utl_file.tmpdir(), 'regressflush_orafce.txt');

SET ROLE TO DEFAULT;
DROP ROLE test_role_files;

DROP FUNCTION checkFlushFile(text);
DELETE FROM utl_file.utl_file_dir;

-- try to use named directory
INSERT INTO utl_file.utl_file_dir(dir, dirname) VALUES(utl_file.tmpdir(), 'TMPDIR');
SELECT gen_file('TMPDIR');
SELECT read_file('TMPDIR');
SELECT utl_file.fremove('TMPDIR', 'regress_orafce.txt');

DROP FUNCTION gen_file(text);
DROP FUNCTION read_file(text);

DELETE FROM utl_file.utl_file_dir;
