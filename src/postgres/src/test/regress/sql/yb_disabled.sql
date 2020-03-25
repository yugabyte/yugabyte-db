--
-- DISABLED FEATURES
--

--
-- NOTIFY/LISTEN
--
NOTIFY channel;
NOTIFY channel, 'message';
SELECT pg_notify('channel', 'text'); -- emits warning, not an error
LISTEN channel;
UNLISTEN channel;
UNLISTEN *;
