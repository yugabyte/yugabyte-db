/*
 * Copyright (c) YugaByte, Inc.
 */

package preflight

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var memory = Memory{"memory", "warning"}

type Memory struct {
	name         string
	warningLevel string
}

func (m Memory) Name() string {
	return m.name
}

func (m Memory) WarningLevel() string {
	return m.warningLevel
}

func (m Memory) Execute() {

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
			log.Fatal(
				fmt.Sprintf(
					"System does not meet the minimum memory limit of %v GB.",
					defaultMinMemoryLimit))
		} else {
			log.Info(fmt.Sprintf("System meets the requirement of %v GB.", defaultMinMemoryLimit))
		}
	}
}

func init() {
	RegisterPreflightCheck(memory)
}
