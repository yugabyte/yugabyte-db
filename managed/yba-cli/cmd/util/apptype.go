/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"strings"
)

// Option represents the options for an app
type Option map[string]string

// AppType represents the app type
type AppType struct {
	Code        string   `json:"code"`
	Type        string   `json:"type"`
	Title       string   `json:"title"`
	Description string   `json:"description"`
	Options     []Option `json:"options"`
}

// AppTypes represents the list of app types
var AppTypes = []AppType{
	{
		Code:  "SqlInserts",
		Type:  "ysql",
		Title: "YSQL",
		Description: "This app writes out 2M unique string keys each with a string value. " +
			"There are multiple readers and writers that write 2M keys and read 1.5M keys. " +
			"To write the keys and read them indefinitely set num_reads & num_writes to -1. " +
			"Note that the number of reads and writes to perform can be specified as a parameter.",
		Options: []Option{
			{"num_unique_keys": "2000000"},
			{"num_reads": "1500000"},
			{"num_writes": "2000000"},
			{"num_threads_read": "32"},
			{"num_threads_write": "2"},
		},
	},
	{
		Code:  "CassandraKeyValue",
		Type:  "cassandra",
		Title: "YCQL",
		Description: "This app writes out 2M unique string keys each with a string value. " +
			"There are multiple readers and writers that update 2M " +
			"keys and read 1.5M keys. " +
			"To update the keys and read them indefinitely set num_reads & num_writes to -1. " +
			"Note that the number of reads and writes to perform can be specified as a parameter.",
		Options: []Option{
			{"num_unique_keys": "2000000"},
			{"num_reads": "1500000"},
			{"num_writes": "2000000"},
			{"num_threads_read": "24"},
			{"num_threads_write": "2"},
			{"table_ttl_seconds": "-1"},
		},
	},
}

// GetAppTypeByCode returns the AppType based on the code
func GetAppTypeByCode(code string) AppType {
	if strings.Compare(strings.ToLower(code), strings.ToLower(YCQLWorkloadType)) == 0 {
		code = "cassandra"
	}
	for _, appType := range AppTypes {
		if strings.Compare(strings.ToLower(appType.Type), strings.ToLower(code)) == 0 {
			return appType
		}
	}
	return AppType{}
}
