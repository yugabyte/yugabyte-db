### pg_cron v1.6.2 (October 20, 2023) ###

* Fixes off-by-1 issue in day of month

### pg_cron v1.6.1 (September 26, 2023) ###

* Restart the pg_cron scheduler if cancelled
* Fix the schema version to 1.6 (requires ALTER EXTENSION pg_cron UPDATE)

### pg_cron v1.6.0 (August 29, 2023) ###

* Adds a cron.launch_active_jobs setting, by Bertrand Drouvot
* Adds support for PostgreSQL 16, by Cristoph Berg & zhjwpku
* Adds scheduling on the last day of the month, by zhjwpku
* Fixes a possible memory corruption bug, by zhjwpku

### pg_cron v1.5.2 (April 9, 2023) ###

* Fixes a bug that caused crashes after upgrading binaries to 1.5, by Polina Bungina

### pg_cron v1.5.1 (February 9, 2023) ###

* Fixes a bug that caused incorrect parsing of some crons schedules

### pg_cron v1.5.0 (February 7, 2023) ###

* Adds the possibility of scheduling a job with a 1-59 second interval
* Adds a cron.timezone setting to configure the timezone of cron schedules
* Removes pg_stat_activity reporting of internal pg_cron metadata queries
* Fixes a bug that caused issues with long job names
* Fixes a bug that caused inactive @reboot jobs to still run
* Fixes a bug that could limit concurrency for background workers
* Fixes a bug that prevented compiling on ARM
* Fixes regression tests for PostgreSQL <= 12

### pg_cron v1.4.2 (July 15, 2022) ###

* Fixes a bug that could lead to privilege escalation if users can trigger CREATE EXTENSION
* Add compatibility for PostgreSQL 15
* Fixes a bug that could cause unschedule to crash
* Ensures that cron.max_running_jobs is not higher than possible connection count

### pg_cron v1.4.1 (September 25, 2021) ###

* Fixes PostgreSQL 11- support

### pg_cron v1.4.0 (September 16, 2021) ###

* Adds a cron.alter_job function to change job properties, by Bertrand Drouvot
* Adds a cron.schedule_in_database function to schedule in a custom database, by Bertrand Drouvot
* Adds a cron.log_min_messages setting to control log_min_messages in pg_cron launcher, by Bertrand Drouvot
* Adds a cron.enable_superuser_jobs setting to disallow superuser jobs
* Fixes a bug that could cause jobs to hang when using cron.use_background_workers, by Bertrand Drouvot
* Fixes a small memory allocation bug, by @mrdrivingduck
* PostgreSQL 14 is supported (no changes were needed)

### pg_cron v1.3.1 (March 29, 2021) ###

* Fixes a memory leak

### pg_cron v1.3.0 (September 30, 2020) ###

* Background worker support by Bertrand Drouvot
* Audit table support by Bertrand Drouvot
* PostgreSQL 13 support by Alexander Kukushkin
* Schedule jobs by name
* Fixes a bug that could cause cron.schedule to crash with long schedules
* Fixes a bug that could cause cron.schedule to get into an infinite loop
* Fixes a bug that caused overlapping runs not to start

### pg_cron v1.2.0 (August 30, 2019) ###

* PostgreSQL 12 support by dverite
* Fixes a bug that caused the cron.job table to not appear in pg_dump

### pg_cron v1.1.4 (April 4, 2019) ###

* Adds a cron.host setting to make the postgres host configurable
* Fixes a bug that could cause segmentation fault after cron.unschedule

### pg_cron v1.1.3 (November 15, 2018) ###

* Fixes a bug that causes pg_cron to run during pg_upgrade
* Fixes a bug that causes pg_cron to show up incorrectly in pg_stat_activity in PG11

### pg_cron v1.1.2 (July 10, 2018) ###

* PostgreSQL 11 support by dverite
* Fix a clang build error by kxjhlele

### pg_cron v1.1.1 (June 7, 2018) ###

* Fixed a bug that would cause new jobs to be created as inactive

### pg_cron v1.1.0 (March 22, 2018) ###

* Add new 'active' column on cron.job table to enable or disable job(s).
* Added a regression test, simply run 'make installcheck'
* Set relevant application_name in pg_stat_activity
* Increased pg_cron version to 1.1

### pg_cron v1.0.2 (October 6, 2017) ###

* PostgreSQL 10 support
* Restrict the maximum number of concurrent tasks
* Ensure table locks on cron.job are kept after schedule/unschedule

### pg_cron v1.0.1 (June 30, 2017) ###

* Fixes a memory leak that occurs when a connection fails immediately
* Fixes a memory leak due to switching memory context when loading metadata
* Fixes a segmentation fault that can occur when using an error message after PQclear

### pg_cron v1.0.0 (January 27, 2017) ###

* Use WaitLatch instead of pg_usleep when there are no tasks

### pg_cron v1.0.0-rc.1 (December 14, 2016) ###

* Initial 1.0 candidate
