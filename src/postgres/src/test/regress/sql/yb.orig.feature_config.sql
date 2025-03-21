--
-- YB_FEATURE Testsuite
--   An introduction on whether or not a feature is supported in YugaByte.
--   This test suite does not go in depth for each command.
--
-- SET / RESET / SHOW configuration
--   Implementation-wise, Postgres groups configuration variables by their datatypes.
--   There are five groups listed in file "guc.c".
--   * In this test, we use one variable from each group in our tests.
--   * In the future, each variable and its affect should be tested properly because each variable
--     is associated with a different set of operators (function oid).
--
--   Steps in this test
--   * First show the existing values.
--   * Change them in a transaction.
--   * Show the new values.
--   * Reset to default.
--
-------------------------------------------------------------------------------
-- Show default values
-------------------------------------------------------------------------------
-- Variables of Type Boolean
SHOW enable_seqscan;
SHOW ssl;
-- Variables of Type Int
SHOW deadlock_timeout;
SHOW ssl_renegotiation_limit;
-- Variables of Type Real
SHOW seq_page_cost;
-- Variables of Type String
SHOW dynamic_library_path;
SHOW ssl_ciphers;
-- Variables of Type Enum
SHOW bytea_output;
-------------------------------------------------------------------------------
-- Some variables can only be set at the startup time.
-------------------------------------------------------------------------------
BEGIN;
		SET ssl = false;
		SET ssl_renegotiation_limit = 7;
		SET ssl_ciphers = "HIGH:MEDIUM";
END;
-------------------------------------------------------------------------------
-- SET LOCAL (Value lifetime: Transaction)
-------------------------------------------------------------------------------
BEGIN;
		SET LOCAL enable_seqscan = FALSE;
		SET LOCAL deadlock_timeout = 10000;
		SET LOCAL seq_page_cost = 7.7;
		SET LOCAL dynamic_library_path = "/tmp/data";
		SET LOCAL bytea_output = "escape";
-- SET LOCAL: Show values while inside the transaction.
		SHOW enable_seqscan;
		SHOW deadlock_timeout;
		SHOW seq_page_cost;
		SHOW dynamic_library_path;
		SHOW bytea_output;
END;
-------------------------------------------------------------------------------
-- SET LOCAL: Show values after the transaction ended.
-- All set values should go back to DEFAULT
-------------------------------------------------------------------------------
SHOW enable_seqscan;
SHOW deadlock_timeout;
SHOW seq_page_cost;
SHOW dynamic_library_path;
SHOW bytea_output;
-------------------------------------------------------------------------------
-- SET SESSION (Value lifetime: Session)
-------------------------------------------------------------------------------
BEGIN;
		SET enable_seqscan = FALSE;
		SET deadlock_timeout = 10000;
		SET seq_page_cost = 7.7;
		SET dynamic_library_path = "/tmp/data";
		SET bytea_output = "escape";
-- SET SESSION: Show values while inside the transaction.
		SHOW enable_seqscan;
		SHOW deadlock_timeout;
		SHOW seq_page_cost;
		SHOW dynamic_library_path;
		SHOW bytea_output;
END;
-------------------------------------------------------------------------------
-- SET SESSION: Show values after the transaction ended.
-- All set values should stay till end of session.
-------------------------------------------------------------------------------
SHOW enable_seqscan;
SHOW deadlock_timeout;
SHOW seq_page_cost;
SHOW dynamic_library_path;
SHOW bytea_output;
-------------------------------------------------------------------------------
-- RESET all variables to default valuse.
-------------------------------------------------------------------------------
BEGIN;
		RESET ALL;
END;
SHOW enable_seqscan;
SHOW deadlock_timeout;
SHOW seq_page_cost;
SHOW dynamic_library_path;
SHOW bytea_output;
-------------------------------------------------------------------------------
-- Check ROLLBACK affect due to errors.
-------------------------------------------------------------------------------
BEGIN;
		SET enable_seqscan = FALSE;
		SET deadlock_timeout = 10000;
		SET seq_page_cost = 7.7;
		SET dynamic_library_path = "/tmp/data";
		SET bytea_output = "escape";
		ROLLBACK;
END;
SHOW enable_seqscan;
SHOW deadlock_timeout;
SHOW seq_page_cost;
SHOW dynamic_library_path;
SHOW bytea_output;
-------------------------------------------------------------------------------
-- To be safe, RESET variables before exit test.
-------------------------------------------------------------------------------
RESET ALL;
