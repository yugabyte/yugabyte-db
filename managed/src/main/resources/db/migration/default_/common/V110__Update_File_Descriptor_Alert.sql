-- Copyright (c) YugabyteDB, Inc.
update alert_definition set query = replace(query, ' * 100', ''), config_written = false
 where query like '%ybp_health_check_used_fd_pct%';
