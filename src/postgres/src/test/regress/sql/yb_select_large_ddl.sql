--
-- SELECT
--
-- table should be full and have indexes created
SELECT COUNT(*) FROM airports;
SELECT * FROM pg_indexes WHERE tablename='airports';
