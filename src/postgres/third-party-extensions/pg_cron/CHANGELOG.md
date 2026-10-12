### pg_cron v1.6.8 (September 8, 2026) ###

* Add PostgreSQL 19 support by @CyberDem0n in https://github.com/citusdata/pg_cron/pull/430
* Add native Windows build support by @codercms in https://github.com/citusdata/pg_cron/pull/431
* Fix native Windows regression tests to use the pg_cron test database
* Improve MinGW build support by @robe2 and @rjd15372 in https://github.com/citusdata/pg_cron/pull/428, https://github.com/citusdata/pg_cron/pull/437, and https://github.com/citusdata/pg_cron/pull/439
* Add the cron.dom_dow_and_logic setting by @vdjakovic in https://github.com/citusdata/pg_cron/pull/421
* Fix a shutdown hang with synchronous replication by @CyberDem0n in https://github.com/citusdata/pg_cron/pull/414
* Fix a shared-memory queue handle leak in the launcher process by @dubek in https://github.com/citusdata/pg_cron/pull/420
* Invalidate the job cache when the relation ID is invalid by @za-arthur in https://github.com/citusdata/pg_cron/commit/f6771508067390433b68ab1ca9cf2269018da42d
* Fix use of an uninitialized variable by @CyberDem0n in https://github.com/citusdata/pg_cron/pull/413
* Preserve compatibility with the v1.3 cron.schedule named-job signature by @CyberDem0n in https://github.com/citusdata/pg_cron/pull/411
* Keep role ownership checks case-sensitive by @ihalatci in https://github.com/citusdata/pg_cron/pull/425
* Fix getrlimit return value handling by @GalenSha in https://github.com/citusdata/pg_cron/pull/436

### pg_cron v1.6.7 (September 4, 2025) ###

* Fix compile errors on newer versions of GCC @marcoslot in https://github.com/citusdata/pg_cron/pull/403

### pg_cron v1.6.6 (September 4, 2025) ###

* Postgres 18 support by @AndrewJackson2020 @esiaero @francoricci
* Stop logging notices and repeating log messages by @marcoslot in https://github.com/citusdata/pg_cron/pull/378
* Fixes a possible crash for unavailable cron jobs @SeleznevPavel in https://github.com/citusdata/pg_cron/pull/370
* Fix build issue FreeBSD 14.3 @francoricci in https://github.com/citusdata/pg_cron/pull/394
* Fix type mismatch in dsm_attach() argument by using DatumGetUInt32() by @yjhjstz in https://github.com/citusdata/pg_cron/pull/395

### pg_cron v1.6.5 (December 12, 2024) ###

* Fix superuser check before adding job to CronJobHash by @CyberDem0n in https://github.com/citusdata/pg_cron/pull/367
* Fix leap year scheduling problem by @zhjwpku in https://github.com/citusdata/pg_cron/pull/365
* Fix possible buffer underflow issue in cron parsing by @marcoslot in https://github.com/citusdata/pg_cron/commit/5c10a8a24527b79c301eaeb04317846f2426dcd5
* Fix compilation warnings by @reshke in https://github.com/citusdata/pg_cron/pull/363

### pg_cron v1.6.4 (August 9, 2024) ###

* Fix bug with invalidation of CachedCronJobRelationId by @CyberDem0n in https://github.com/citusdata/pg_cron/pull/346
* Select procedure based on argument type of cron_unschedule_named by @CyberDem0n in https://github.com/citusdata/pg_cron/pull/347
* Revert "Remove unnecessary lastStartTime reset" d90843de92d5e517a23b1e17da56dc08c496c774 per https://github.com/citusdata/pg_cron/issues/342

### pg_cron v1.6.3 (July 23, 2024) ###

* Fix pointer reuse bug causing off-by-1 day of month by @marcoslot in https://github.com/citusdata/pg_cron/pull/292
* Update the database when scheduling an existing job by @nuno-faria in https://github.com/citusdata/pg_cron/pull/293
* Make cron_unschedule_named() accept v1.4 SQL signature. by @nmisch in https://github.com/citusdata/pg_cron/pull/299
* Allow interrupts in pg_cron launcher loop to avoid deadlock by @Ngalstyan4 in https://github.com/citusdata/pg_cron/pull/319
* Fixes possible overflow by @sminux in https://github.com/citusdata/pg_cron/pull/326
* Log start & end time for all failed runs by @kketch in https://github.com/citusdata/pg_cron/pull/324
* Replace MemoryContextResetAndDeleteChildren macro with MemoryContextReset for PG 17 compatibility by @esiaero in https://github.com/citusdata/pg_cron/pull/332
* Fix compiler error on Illumos by @japinli in https://github.com/citusdata/pg_cron/pull/317

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
