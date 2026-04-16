/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

// SpecificGFlagsCLIOutput represents the CLI output for specific gflags
type SpecificGFlagsCLIOutput struct {
	SpecificGFlags SpecificGFlagsCLI
	ClusterType    string
	ClusterUUID    string
}

// SpecificGFlagsCLI represents the CLI input for specific gflags
type SpecificGFlagsCLI struct {
	InheritFromPrimary *bool `json:"inheritFromPrimary,omitempty"`
	// Overrides for gflags per availability zone
	PerAZ           *map[string]PerProcessFlags `json:"perAZ,omitempty"`
	PerProcessFlags *PerProcessFlags            `json:"perProcessFlags,omitempty"`
}

// PerProcessFlags represents the gflags per process
type PerProcessFlags map[string]map[string]string
