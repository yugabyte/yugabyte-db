-- Copyright (c) YugabyteDB, Inc.

DELETE FROM runtime_config_entry
WHERE path = 'yb.ui.xcluster.dr.show_xcluster_config';
