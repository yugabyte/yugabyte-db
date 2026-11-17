// Copyright (c) YugabyteDB, Inc.

// Package cdcstate dumps the cdc_state system table to CSV for a support bundle. It runs on a DB
// node and reads the local tserver through yb-ts-cli, so it needs neither YCQL credentials nor
// YCQL enabled.
//
// Nothing is held whole in memory: rows stream from each dump into the CSV, and list_tablets is
// parsed a line at a time, because the helper runs under the bundle cgroup's memory.high.
//
// Input: `yb-ts-cli dump_tablet_data` writes a text dump; rows follow the bare line "Rows:", a
// blank or "Table Info:" line ends a run, and "Row count:" or "XOR hash:" stops the scan. A row is
// not CSV: its fourth field is an inline map whose entries are comma-separated too, e.g.
// {string:"xmin" -> string:"42"}, and <NULL> means absent. So smartSplit tracks quotes and brace
// depth by hand; the output side uses encoding/csv.
//
// Contract with collect_cdc_state in support_bundle_utils.sh: the CSV goes to --output, progress
// and errors to stderr (YBA logs only stderr on failure). `smoke` prints exactly "ok". Exit 0
// success, 1 runtime failure, 2 missing argument. There is no fallback: a failure fails cdc_state
// on that node.
package cdcstate
