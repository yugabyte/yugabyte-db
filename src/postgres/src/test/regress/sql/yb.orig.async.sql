--
-- YB LISTEN/NOTIFY tests
--

-- LISTEN should not depend on yugabyte role
\c - postgres;
ALTER ROLE yugabyte NOLOGIN;
LISTEN c;
-- reset
ALTER ROLE yugabyte LOGIN;

-- Add new tests before the above test.
