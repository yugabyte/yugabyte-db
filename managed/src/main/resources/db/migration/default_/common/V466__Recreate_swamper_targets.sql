-- Copyright (c) YugaByte, Inc.

-- Add docdb_keys_found and docdb_obsolete_keys_found_past_cutoff to priority_regex
update universe set swamper_config_written = false;
