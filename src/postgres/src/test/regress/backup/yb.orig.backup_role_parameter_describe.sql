\dt

------------------------------------------------
-- Test source log_min_messages.
------------------------------------------------
SHOW log_min_messages;

\c yugabyte yugabyte
SHOW log_min_messages;

\c yugabyte admin
SHOW log_min_messages;

\c yugabyte developer
SHOW log_min_messages;

------------------------------------------------
-- Test restored log_min_messages.
------------------------------------------------
\c db2 yugabyte
SHOW log_min_messages;

\c db2 admin
SHOW log_min_messages;

\c db2 developer
SHOW log_min_messages;
