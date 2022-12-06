/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"runtime"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var defaultMinCPUs int = 4

var Cpu = &cpuCheck{"cpu", "warning"}

type cpuCheck struct {
	name         string
	warningLevel string
}

func (c cpuCheck) Name() string {
	return c.name
}

func (c cpuCheck) WarningLevel() string {
	return c.warningLevel
}

func (c cpuCheck) Execute() error {

	if runtime.NumCPU() < defaultMinCPUs {
		return fmt.Errorf("System currently has %v CPU but requires %v CPUs.",
			runtime.NumCPU(), defaultMinCPUs)
	} else {
		log.Info(fmt.Sprintf("System meets the requirement of %v Virtual CPUs!", defaultMinCPUs))
	}
	return nil
}
