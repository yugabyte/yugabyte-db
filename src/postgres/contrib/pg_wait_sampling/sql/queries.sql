CREATE EXTENSION pg_wait_sampling;

WITH t as (SELECT sum(0) FROM pg_wait_sampling_current)
	SELECT sum(0) FROM generate_series(1, 2), t;

WITH t as (SELECT sum(0) FROM pg_wait_sampling_history)
	SELECT sum(0) FROM generate_series(1, 2), t;

WITH t as (SELECT sum(0) FROM pg_wait_sampling_profile)
	SELECT sum(0) FROM generate_series(1, 2), t;

-- Some dummy checks just to be sure that all our functions work and return something.
SELECT count(*) = 1 as test FROM pg_wait_sampling_get_current(pg_backend_pid());
SELECT count(*) >= 0 as test FROM pg_wait_sampling_get_profile();
SELECT count(*) >= 0 as test FROM pg_wait_sampling_get_history();
SELECT pg_wait_sampling_reset_profile();

DROP EXTENSION pg_wait_sampling;
