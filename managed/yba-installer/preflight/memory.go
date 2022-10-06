/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
	 "strconv"
	 "strings"
	 "fmt"
  )
 
 var memory = Memory{"memory", "warning"}
 
 type Memory struct {
	 Name string
	 WarningLevel string
 }
 
 
 func (m Memory) GetName() string {
	 return m.Name
 }
 
 func (m Memory) GetWarningLevel() string {
	 return m.WarningLevel
 }
 
 func (m Memory) Execute() {
 
	command := "grep"
    args := []string{"MemTotal", "/proc/meminfo"}
    output, err := ExecuteBashCommand(command, args)
    if err != nil {
        LogError(err.Error())
    } else {
        field1 := strings.Fields(output)[1]
        availableMemoryKB, _ := strconv.Atoi(strings.Split(field1, " ")[0])
        availableMemoryGB := float64(availableMemoryKB) / 1e6
        if availableMemoryGB < defaultMinMemoryLimit {
            LogError(fmt.Sprintf("System does not meet the minimum memory limit of %v GB.", defaultMinMemoryLimit))
        } else {
            LogInfo(fmt.Sprintf("System meets the requirement of %v GB.", defaultMinMemoryLimit))
        }
    }
 }