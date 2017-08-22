\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

SELECT 'msg1' FROM pg_logical_emit_message(true, 'wal2json', 'this is a\ message');
SELECT 'msg2' FROM pg_logical_emit_message(false, 'wal2json', 'this is "another" message');

BEGIN;
SELECT 'msg3' FROM pg_logical_emit_message(true, 'wal2json', 'this message will not be printed');
SELECT 'msg4' FROM pg_logical_emit_message(false, 'wal2json', 'this message will be printed even if the transaction is rollbacked');
ROLLBACK;

BEGIN;
SELECT 'msg5' FROM pg_logical_emit_message(true, 'wal2json', 'this is message #1');
SELECT 'msg6' FROM pg_logical_emit_message(false, 'wal2json', 'this message will be printed before message #1');
SELECT 'msg7' FROM pg_logical_emit_message(true, 'wal2json', 'this is message #2');
COMMIT;

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'pretty-print', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
