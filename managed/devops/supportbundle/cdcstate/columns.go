// Copyright (c) YugabyteDB, Inc.

package cdcstate

// MAPColumns and ALLColumns match CDCStateTableEntry in cdc_state_table.h.
var MAPColumns = []string{
	"active_time",
	"cdc_sdk_safe_time",
	"snapshot_key",
	"confirmed_flush_lsn",
	"restart_lsn",
	"xmin_val",
	"record_id_commit_time",
	"last_pub_refresh_time",
	"pub_refresh_times",
	"last_decided_pub_refresh_time",
	"start_hash_range",
	"end_hash_range",
	"active_pid",
}

var mapKeyToColumn = map[string]string{
	"xmin": "xmin_val",
}

// ALLColumns is the fixed CSV column order.
var ALLColumns = append(
	append([]string{}, "tablet_id", "stream_id", "checkpoint"),
	append(append([]string{}, MAPColumns...), "last_replication_time")...,
)
