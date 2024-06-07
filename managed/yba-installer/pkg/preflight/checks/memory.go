/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

var defaultMinMemoryLimit float64 = 8

// Memory check initialized
var Memory = &memoryCheck{"memory", true}

type memoryCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (m memoryCheck) Name() string {
	return m.name
}

// SkipAllowed gets if the check can be skipped.
func (m memoryCheck) SkipAllowed() bool {
	return m.skipAllowed
}

// Execute validates there is enough memory for yba
func (m memoryCheck) Execute() Result {
	res := Result{
		Check:  m.name,
		Status: StatusPassed,
	}
	command := "grep"
	args := []string{"MemTotal", "/proc/meminfo"}
	out := shell.Run(command, args...)
	if !out.SucceededOrLog() {
		res.Error = out.Error
		res.Status = StatusCritical
	} else {
		field1 := strings.Fields(out.StdoutString())[1]
		availableMemoryKB, _ := strconv.Atoi(strings.Split(field1, " ")[0])
		availableMemoryGB := float64(availableMemoryKB) / 1e6
		if availableMemoryGB < defaultMinMemoryLimit {
			err := fmt.Errorf("System does not meet the minimum memory limit of %v GB.",
				defaultMinMemoryLimit)
			res.Error = err
			res.Status = StatusCritical
		} else {
			log.Info(fmt.Sprintf("System meets the requirement of %v GB.", defaultMinMemoryLimit))
		}
	}
	return res
}
