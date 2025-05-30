CREATE FOREIGN DATA WRAPPER dummy;

CREATE SCHEMA foreign_schema;
CREATE SERVER s0 FOREIGN DATA WRAPPER dummy;

CREATE TABLE fd_pt2 (
	c1 integer NOT NULL,
	c2 text,
	c3 date
) PARTITION BY LIST (c1);
CREATE FOREIGN TABLE fd_pt2_1 PARTITION OF fd_pt2 FOR VALUES IN (1)
  SERVER s0 OPTIONS (delimiter ',', quote '"', "be quoted" 'value');

-- TRUNCATE doesn't work on foreign tables, either directly or recursively
TRUNCATE fd_pt2_1;  -- ERROR
TRUNCATE fd_pt2;  -- ERROR
