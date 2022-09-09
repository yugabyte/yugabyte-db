/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
	 "strconv"
	 "strings"
	 "fmt"
  )
 
 var ssd = Ssd{"ssd", "warning"}
 
 type Ssd struct {
	 Name string
	 WarningLevel string
 }
 
 func (s Ssd) GetName() string {
	 return s.Name
 }
 
 func (s Ssd) GetWarningLevel() string {
	 return s.WarningLevel
 }
 
 func (s Ssd) Execute() {
 
	command := "df"
    args := []string{"-H", "--total"}
    output, err := ExecuteBashCommand(command, args)
    if err != nil {
        LogError(err.Error())
    } else {
        totalIndex := IndexOf(strings.Fields(output), "total")
        sto_str := strings.Split(strings.Fields(output)[totalIndex+1], " ")[0]
        units := string(sto_str[len(sto_str)-1])
        availableSSDstorage, _ := strconv.ParseFloat(sto_str[:len(sto_str)-1], 64)
        if units == "T" {
            availableSSDstorage *= 1024
        }
        if availableSSDstorage < defaultMinSSDStorage {
            LogError(fmt.Sprintf("System does not meet the minimum available SSD storage of %v GB.", defaultMinSSDStorage))
        } else {
            LogInfo(fmt.Sprintf("System meets the minimum available SSD storage of %v GB.", defaultMinSSDStorage))
        }
    }
 }