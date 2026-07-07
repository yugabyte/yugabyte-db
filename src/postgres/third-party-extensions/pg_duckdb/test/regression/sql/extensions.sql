DROP EXTENSION pg_duckdb CASCADE;
CREATE EXTENSION pg_duckdb;

SET duckdb.force_execution TO false;
SET duckdb.allow_community_extensions = true;

SELECT * FROM duckdb.query($$ SELECT extension_name, loaded, installed, installed_from FROM duckdb_extensions() WHERE loaded and extension_name != 'jemalloc' $$);

SELECT last_value FROM duckdb.extensions_table_seq;

-- INSERT SHOULD TRIGGER UPDATE OF EXTENSIONS

SELECT duckdb.install_extension('icu');

-- Increases the sequence twice because we use ON CONFLICT DO UPDATE. So
-- the trigger fires for both INSERT and UPDATE internally.
SELECT last_value FROM duckdb.extensions_table_seq;

SELECT * FROM duckdb.query($$ SELECT extension_name, loaded, installed, installed_from FROM duckdb_extensions() WHERE loaded and extension_name != 'jemalloc' $$);

-- Check that we can rerun this without issues
SELECT duckdb.install_extension('icu');

-- Increases the sequence twice because we use ON CONFLICT DO UPDATE. So
-- the trigger fires for both INSERT and UPDATE internally.
SELECT last_value FROM duckdb.extensions_table_seq;

SELECT * FROM duckdb.query($$ SELECT extension_name, loaded, installed, installed_from FROM duckdb_extensions() WHERE loaded and extension_name != 'jemalloc' $$);


SELECT duckdb.install_extension('aws');

SELECT last_value FROM duckdb.extensions_table_seq;

SELECT * FROM duckdb.query($$ SELECT extension_name, loaded, installed, installed_from FROM duckdb_extensions() WHERE loaded and extension_name != 'jemalloc' $$);

-- autoload_extension and load_extension function should work as expected
SELECT * FROM duckdb.query($$ FROM duckdb_extensions() SELECT installed, loaded WHERE extension_name = 'aws' $$);
SELECT duckdb.autoload_extension('aws', 'false');
CALL duckdb.recycle_ddb();
SELECT * FROM duckdb.query($$ FROM duckdb_extensions() SELECT installed, loaded WHERE extension_name = 'aws' $$);
SELECT duckdb.load_extension('aws');
SELECT * FROM duckdb.query($$ FROM duckdb_extensions() SELECT installed, loaded WHERE extension_name = 'aws' $$);

-- Turning it back on should autoload it again
SELECT duckdb.autoload_extension('aws', 'true');
CALL duckdb.recycle_ddb();
SELECT * FROM duckdb.query($$ FROM duckdb_extensions() SELECT installed, loaded WHERE extension_name = 'aws' $$);

-- Autoloading is only supported for already known extensions
SELECT duckdb.autoload_extension('doesnotexist', 'true');

-- DELETE SHOULD TRIGGER UPDATE OF EXTENSIONS
DELETE FROM duckdb.extensions WHERE name = 'aws';

SELECT last_value FROM duckdb.extensions_table_seq;

SELECT * FROM duckdb.query($$ SELECT extension_name, loaded, installed, installed_from FROM duckdb_extensions() WHERE loaded and extension_name != 'jemalloc' $$);

-- Reconnecting should cause the extension not to be loaded anymore
CALL duckdb.recycle_ddb();
SELECT * FROM duckdb.query($$ SELECT extension_name, loaded, installed, installed_from FROM duckdb_extensions() WHERE loaded and extension_name != 'jemalloc' $$);

SELECT duckdb.install_extension('prql', 'community');

SELECT last_value FROM duckdb.extensions_table_seq;

SELECT * FROM duckdb.query($$ SELECT extension_name, loaded, installed, installed_from FROM duckdb_extensions() WHERE loaded and extension_name != 'jemalloc' $$);

-- cleanup
TRUNCATE duckdb.extensions;
