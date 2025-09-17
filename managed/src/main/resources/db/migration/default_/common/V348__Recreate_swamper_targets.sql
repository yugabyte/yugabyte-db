-- Copyright (c) YugabyteDB, Inc.

-- Remove exported_exported_instance metric label
update universe set swamper_config_written = false;
