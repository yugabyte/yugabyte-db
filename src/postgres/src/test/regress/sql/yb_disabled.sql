--
-- DISABLED FEATURES
--

--
-- NOTIFY/LISTEN
--
NOTIFY channel;
NOTIFY channel, 'message';
SELECT pg_notify('channel', 'text');
LISTEN channel;
UNLISTEN channel;
UNLISTEN *;
