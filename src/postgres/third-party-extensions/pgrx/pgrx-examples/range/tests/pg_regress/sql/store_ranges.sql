DROP TABLE IF EXISTS store_ranges;
CREATE TABLE store_ranges
(
    id serial8,
    r  int4range
);

INSERT INTO store_ranges (r)
SELECT range.range(100, 100 + x)
FROM generate_series(0, 100) x;
SELECT *
FROM store_ranges;