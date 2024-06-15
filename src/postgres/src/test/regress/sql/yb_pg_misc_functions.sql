--
-- pg_log_backend_memory_contexts()
--
-- Memory contexts are logged and they are not returned to the function.
-- Furthermore, their contents can vary depending on the timing. However,
-- we can at least verify that the code doesn't fail.
--
SELECT * FROM pg_log_backend_memory_contexts(pg_backend_pid());
