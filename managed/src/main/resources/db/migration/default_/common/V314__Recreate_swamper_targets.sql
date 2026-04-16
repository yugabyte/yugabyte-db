-- Copyright (c) YugaByte, Inc.

-- Add consumer_safe_time_[^_]+ to priority_regex
update universe set swamper_config_written = false;
