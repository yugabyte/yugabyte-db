CREATE TYPE part.partition_type AS ENUM ('time', 'id');
CREATE TYPE part.partition_interval AS ENUM ('yearly', 'monthly', 'weekly', 'daily', 'hourly', 'half-hour', 'quarter-hour', 'id');
