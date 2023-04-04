--
-- Test to check default for pid and query_id
--
SELECT is_yb_pg_tracing_enabled((SELECT pg_backend_pid()));
SELECT yb_pg_enable_tracing(NULL);
SELECT is_yb_pg_tracing_enabled(query_id => NULL);
SELECT yb_pg_disable_tracing();

--
-- Test to enable and disable tracing and for current backend for all queries
--
SELECT yb_pg_enable_tracing(pid => NULL, query_id => NULL);
SELECT yb_pg_enable_tracing(query_id => NULL, pid => (SELECT pg_backend_pid()));
SELECT is_yb_pg_tracing_enabled((SELECT pg_backend_pid()), NULL);
SELECT yb_pg_disable_tracing();

--
-- Test to enable and disabled tracing on all backends
--
SELECT yb_pg_enable_tracing(0, NULL);
SELECT is_yb_pg_tracing_enabled(pid => 0, query_id => NULL);
SELECT yb_pg_disable_tracing(query_id => NULL, pid => 0);

--
-- Test to check invalid pid
--
SELECT yb_pg_enable_tracing(-100, query_id => NULL);
SELECT yb_pg_disable_tracing(-1000, query_id => NULL);
SELECT is_yb_pg_tracing_enabled(-10000, query_id => NULL);

--
-- Test to enable and disable tracing for a query id for current backend
--
SELECT yb_pg_enable_tracing(query_id => 12345);
SELECT yb_pg_enable_tracing(query_id => 12345);
SELECT is_yb_pg_tracing_enabled(query_id => 12345);
SELECT yb_pg_disable_tracing(query_id => 12345);
SELECT is_yb_pg_tracing_enabled(query_id => 12345);

--
-- Test to disable a query id which was not enabled for current backend
-- This will throw a warning
--
SELECT is_yb_pg_tracing_enabled(query_id => 12346789);
SELECT yb_pg_disable_tracing(query_id => 12346789);

--
-- Test to enable and disable tracing for a query id for all backends
-- This will throw a warning as the same query_id enabled twice
--
SELECT yb_pg_enable_tracing(0, query_id => 12345);
SELECT yb_pg_enable_tracing(0, query_id => 12345);
SELECT is_yb_pg_tracing_enabled(0, query_id => 12345);
SELECT yb_pg_disable_tracing(0, query_id => 12345);
SELECT is_yb_pg_tracing_enabled(0, query_id => 12345);

--
-- Test to enable tracing for one backend and disable for all backends
--
SELECT yb_pg_enable_tracing();
SELECT is_yb_pg_tracing_enabled(NULL);
SELECT yb_pg_disable_tracing(0);
SELECT is_yb_pg_tracing_enabled();
