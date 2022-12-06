/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var defaultMinMemoryLimit float64 = 15

var Memory = &memoryCheck{"memory", "warning"}

type memoryCheck struct {
	name         string
	warningLevel string
}

func (m memoryCheck) Name() string {
	return m.name
}

func (m memoryCheck) WarningLevel() string {
	return m.warningLevel
}

func (m memoryCheck) Execute() error {

	command := "grep"
	args := []string{"MemTotal", "/proc/meminfo"}
	output, err := common.ExecuteBashCommand(command, args)
	if err != nil {
		log.Fatal(err.Error())
	} else {
		field1 := strings.Fields(output)[1]
		availableMemoryKB, _ := strconv.Atoi(strings.Split(field1, " ")[0])
		availableMemoryGB := float64(availableMemoryKB) / 1e6
		if availableMemoryGB < defaultMinMemoryLimit {
			return fmt.Errorf("System does not meet the minimum memory limit of %v GB.",
				defaultMinMemoryLimit)
		} else {
			log.Info(fmt.Sprintf("System meets the requirement of %v GB.", defaultMinMemoryLimit))
		}
	}
	return nil
}
