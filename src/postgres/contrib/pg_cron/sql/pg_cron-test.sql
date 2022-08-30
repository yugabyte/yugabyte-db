CREATE EXTENSION pg_cron VERSION '1.0';
SELECT extversion FROM pg_extension WHERE extname='pg_cron';
ALTER EXTENSION pg_cron UPDATE TO '1.4';
SELECT extversion FROM pg_extension WHERE extname='pg_cron';

SET cron.enable_superuser_jobs TO on;

-- Vacuum every day at 10:00am (GMT)
SELECT cron.schedule('0 10 * * *', 'VACUUM');

-- Stop scheduling a job
SELECT cron.unschedule(1);


-- Invalid input: input too long
SELECT cron.schedule(repeat('a', 1000), '');

-- Try to update pg_cron on restart
SELECT cron.schedule('@restar', 'ALTER EXTENSION pg_cron UPDATE');
SELECT cron.schedule('@restart', 'ALTER EXTENSION pg_cron UPDATE');

-- Vacuum every day at 10:00am (GMT)
SELECT cron.schedule('myvacuum', '0 10 * * *', 'VACUUM');

SELECT jobid, jobname, schedule, command FROM cron.job ORDER BY jobid;

-- Make that 11:00am (GMT)
SELECT cron.schedule('myvacuum', '0 11 * * *', 'VACUUM');

SELECT jobid, jobname, schedule, command FROM cron.job ORDER BY jobid;

-- Make that VACUUM FULL
SELECT cron.schedule('myvacuum', '0 11 * * *', 'VACUUM FULL');

SELECT jobid, jobname, schedule, command FROM cron.job ORDER BY jobid;

-- Stop scheduling a job
SELECT cron.unschedule('myvacuum');

SELECT jobid, jobname, schedule, command FROM cron.job ORDER BY jobid;

-- Testing version >= 1.4 new APIs
-- First as superuser

-- Update a job without one job attribute to change
SELECT cron.alter_job(2);

-- Update to a non existing database
select cron.alter_job(job_id:=2,database:='hopedoesnotexist');

-- Create a database that does not allow connection
create database pgcron_dbno;
revoke connect on database pgcron_dbno from public;

-- create a test user
create user pgcron_cront with password 'pwd';
GRANT USAGE ON SCHEMA cron TO pgcron_cront;

-- Schedule a job for this user on the database that does not accept connections
SELECT cron.schedule_in_database(job_name:='can not connect', schedule:='0 11 * * *', command:='VACUUM',database:='pgcron_dbno',username:='pgcron_cront');

-- Create a database that does allow connections
create database pgcron_dbyes;

-- Schedule a job on the database that does accept connections for a non existing user
SELECT cron.schedule_in_database(job_name:='user does not exist', schedule:='0 11 * * *', command:='VACUUM',database:='pgcron_dbyes',username:='pgcron_useraqwxszedc');

-- Alter an existing job on a database that does not accept connections
SELECT cron.alter_job(job_id:=2,database:='pgcron_dbno',username:='pgcron_cront');

-- Make sure pgcron_cront can execute alter_job
GRANT EXECUTE ON FUNCTION cron.alter_job(bigint,text,text,text,text,boolean) TO public;

-- Second as non superuser
SET SESSION AUTHORIZATION pgcron_cront;

-- Create a job
SELECT cron.schedule('My vacuum', '0 11 * * *', 'VACUUM');

-- Create a job for another user
SELECT cron.schedule_in_database(job_name:='his vacuum', schedule:='0 11 * * *', command:='VACUUM',database:=current_database(),username:='anotheruser');

-- Change the username of an existing job that the user own
select cron.alter_job(job_id:=6,username:='anotheruser');

-- Update a job that the user does not own
select cron.alter_job(job_id:=2,database:='pgcron_dbyes');

-- change the database for a job that the user own and can connect to
select cron.alter_job(job_id:=6,database:='pgcron_dbyes');
SELECT database FROM cron.job;

-- change the database for a job that the user own but can not connect to
select cron.alter_job(job_id:=6,database:='pgcron_dbno');
SELECT database FROM cron.job;

-- back to superuser
RESET SESSION AUTHORIZATION;

-- Change the username of an existing job
select cron.alter_job(job_id:=2,username:='pgcron_cront');
SELECT username FROM cron.job where jobid=2;

-- Create a job for another user
SELECT cron.schedule_in_database(job_name:='his vacuum', schedule:='0 11 * * *', command:='VACUUM',database:=current_database(), username:='pgcron_cront');
SELECT username FROM cron.job where jobid=7;

-- Try to schedule a job as superuser when it is not allowed
SET cron.enable_superuser_jobs TO off;

SELECT cron.schedule(job_name:='disallowed-superuser', schedule:='* * * * *', command:='drop database pg_crondbno');
SELECT cron.alter_job(7, username := current_user);

-- Scheduling as other users is allowed as superuser
SELECT cron.schedule_in_database(job_name:='more vacuum', schedule:='0 12 * * *', command:='VACUUM', database:=current_database(), username:='pgcron_cront');
SELECT cron.alter_job(7, username := 'pgcron_cront');

-- cleaning
DROP EXTENSION pg_cron;
drop user pgcron_cront;
drop database pgcron_dbno;
drop database pgcron_dbyes;
