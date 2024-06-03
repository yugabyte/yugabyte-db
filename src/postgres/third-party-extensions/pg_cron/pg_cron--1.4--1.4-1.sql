/* pg_cron--1.4--1.4-1.sql */

/*
 * pg_dump will read from these sequences. Grant everyone permission
 * to read from the sequence. That way, a user with usage on the cron
 * schema can also do pg_dump. This does not grant write/nextval
 * permission.
 */
GRANT SELECT ON SEQUENCE cron.jobid_seq TO public;
GRANT SELECT ON SEQUENCE cron.runid_seq TO public;
