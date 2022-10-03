/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
	 "strings"
	 "regexp"
  )
 
 var python = Python{"python", "critical"}
 
 type Python struct {
	 Name string
	 WarningLevel string
 }
 
 func (p Python) GetName() string {
	 return p.Name
 }
 
 func (p Python) GetWarningLevel() string {
	 return p.WarningLevel
 }
 
 func (p Python) Execute() {
	command := "bash"
    args := []string{"-c", "python3 --version"}
    output, _ := ExecuteBashCommand(command, args)

    outputTrimmed := strings.TrimSuffix(output, "\n")

    re := regexp.MustCompile(`Python 3.6|Python 3.7|Python 3.8|Python 3.9`)

    if !re.MatchString(outputTrimmed) {

        LogError("System does not meet Python requirements. Please install any " +
        "version of Python between 3.6 and 3.9 to continue.")

    } else {

        LogInfo("System meets Python installation requirements.")
    }
 }