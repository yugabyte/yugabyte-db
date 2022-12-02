/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
	 "strconv"
	 "strings"
	 "fmt"

     log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
     "github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
  )

 var memory = Memory{"memory", "warning"}

 type Memory struct {
	 name string
	 WarningLevel string
 }


 func (m Memory) Name() string {
	 return m.name
 }

 func (m Memory) GetWarningLevel() string {
	 return m.WarningLevel
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
