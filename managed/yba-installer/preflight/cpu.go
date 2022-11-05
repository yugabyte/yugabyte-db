/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"fmt"
	"runtime"
)

var cpu = Cpu{"cpu", "warning"}

type Cpu struct {
	Name         string
	WarningLevel string
}

func (c Cpu) GetName() string {
	return c.Name
}

func (c Cpu) GetWarningLevel() string {
	return c.WarningLevel
}

func (c Cpu) Execute() {

	if runtime.NumCPU() < defaultMinCPUs {
		LogError(fmt.Sprintf("System currently has %v CPU but requires %v CPUs.",
			runtime.NumCPU(), defaultMinCPUs))
	} else {
		LogInfo(fmt.Sprintf("System meets the requirement of %v Virtual CPUs!", defaultMinCPUs))
	}
}
