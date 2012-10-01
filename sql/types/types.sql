CREATE TYPE partition_type AS ENUM ('time-static', 'time-dynamic', 'id-static', 'id-dynamic');
CREATE TYPE check_parent_table AS (parent_table text, count bigint);
