CREATE EXTENSION pg_cron;

-- Recreate job with same name
SELECT cron.schedule('myjob', '0 11 * * *', 'SELECT 1');
SELECT cron.unschedule('myjob');
SELECT cron.schedule('myjob', '0 11 * * *', 'SELECT 1');