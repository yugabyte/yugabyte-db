CREATE TYPE part.partition_type AS ENUM ('time-static', 'time-dynamic', 'id-static', 'id-dynamic');
CREATE TYPE part.check_parent_table AS (parent_table text, count bigint);
