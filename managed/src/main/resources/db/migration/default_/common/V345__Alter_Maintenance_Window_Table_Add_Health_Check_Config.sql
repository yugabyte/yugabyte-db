-- Copyright (c) YugaByte, Inc.

-- Add a new column to the maintenance_window table which stores the info on which universes to suppress the health check notifications.
ALTER TABLE maintenance_window ADD COLUMN IF NOT EXISTS suppress_health_check_notifications_config json_alias;
