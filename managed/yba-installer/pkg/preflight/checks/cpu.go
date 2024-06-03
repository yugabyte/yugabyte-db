/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"runtime"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

var defaultMinCPUs int = 4

// The CPU check
var Cpu = &cpuCheck{"cpu", true}

type cpuCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the Name of the check.
func (c cpuCheck) Name() string {
	return c.name
}

// SkipAllowed returns if the user can specify to skip the check (not if they have specified)
func (c cpuCheck) SkipAllowed() bool {
	return c.skipAllowed
}

// Execute validates there are enough cpu resources.
func (c cpuCheck) Execute() Result {
	res := Result{
		Check:  c.name,
		Status: StatusPassed,
	}
	log.Debug(fmt.Sprintf("Found %d CPUs. Minimum is %d", runtime.NumCPU(), defaultMinCPUs))
	if runtime.NumCPU() < defaultMinCPUs {
		err := fmt.Errorf("System currently has %v CPU but requires %v CPUs.",
			runtime.NumCPU(), defaultMinCPUs)
		res.Error = err
		res.Status = StatusCritical
	} else {
		log.Info(fmt.Sprintf("System meets the requirement of %v Virtual CPUs!", defaultMinCPUs))
	}
	return res
}
