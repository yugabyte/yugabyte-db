\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

SELECT 'msg1' FROM pg_logical_emit_message(true, 'wal2json', 'this is a\ message');
SELECT 'msg2' FROM pg_logical_emit_message(false, 'wal2json', 'this is "another" message');

SELECT 'msg3' FROM pg_logical_emit_message(false, 'wal2json', E'\\x546170697275732074657272657374726973'::bytea);
SELECT 'msg4' FROM pg_logical_emit_message(false, 'wal2json', E'\\x5072696f646f6e746573206d6178696d7573'::bytea);
SELECT 'msg5' FROM pg_logical_emit_message(false, 'wal2json', E'\\x436172796f6361722062726173696c69656e7365'::bytea);

BEGIN;
SELECT 'msg6' FROM pg_logical_emit_message(true, 'wal2json', 'this message will not be printed');
SELECT 'msg7' FROM pg_logical_emit_message(false, 'wal2json', 'this message will be printed even if the transaction is rollbacked');
ROLLBACK;

BEGIN;
SELECT 'msg8' FROM pg_logical_emit_message(true, 'wal2json', 'this is message #1');
SELECT 'msg9' FROM pg_logical_emit_message(false, 'wal2json', 'this message will be printed before message #1');
SELECT 'msg10' FROM pg_logical_emit_message(true, 'wal2json', 'this is message #2');
COMMIT;

SELECT 'msg11' FROM pg_logical_emit_message(true, 'filtered', 'this message will be filtered');
SELECT 'msg12' FROM pg_logical_emit_message(true, 'added1', 'this message will be printed');
SELECT 'msg13' FROM pg_logical_emit_message(true, 'added2', 'this message will be filtered');

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'filter-msg-prefixes', 'foo, filtered, bar', 'add-msg-prefixes', 'added1, added3, wal2json');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-msg-prefixes', 'foo, filtered, bar', 'add-msg-prefixes', 'added1, added3, wal2json');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
