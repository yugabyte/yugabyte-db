/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
	 "strconv"
	 "os"
	 "os/exec"
	 "fmt"
  )
 
 var cpu = Cpu{"cpu", "warning"}
 
 type Cpu struct {
	 Name string
	 WarningLevel string
 }
 
 func (c Cpu) GetName() string {
	 return c.Name
 }
 
 func (c Cpu) GetWarningLevel() string {
	 return c.WarningLevel
 }
 
 func (c Cpu) Execute() {
 
	 command := "bash"
	 args := []string{"-c", "cat /proc/cpuinfo | grep processor | wc -l"}
	 cmd := exec.Command(command, args...)
	 cmd.Stderr = os.Stderr
	 output, err := cmd.Output()
	 if err != nil {
		 LogError("Error: " + err.Error() + ".")
	 } else {
		 numberOfvCPUs, _ := strconv.Atoi(string(output[0]))
		 if float64(numberOfvCPUs) < defaultMinCPUs {
			 LogError(fmt.Sprintf("System does not meet the requirement of %v Virtual CPUs.", defaultMinCPUs))
		 } else {
			 LogInfo(fmt.Sprintf("System meets the requirement of %v Virtual CPUs!", defaultMinCPUs))
		 }
	 }
 }