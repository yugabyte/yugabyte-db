--
-- SELECT
--
-- SELECT with LIMIT to check if prefetching hurts performance for a small set of data.
SELECT 1 FROM airports LIMIT 1;
-- Check aggregate function performance for a large set or rows.
SELECT COUNT(*) FROM airports;
-- SELECT ALL to check if prefetching is working fine for a large set of data.
SELECT * FROM airports ORDER BY ident;
-- SELECT ALL with WHERE condition on PRIMARY KEY for a large set of data.
SELECT * FROM airports WHERE ident < '04' AND ident > '01' ORDER BY ident;
-- SELECT ALL with WHERE condition on non PRIMARY KEY for a large set of data.
SELECT * FROM airports WHERE iso_region = 'US-CA' ORDER BY ident;
