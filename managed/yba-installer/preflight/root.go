/*
 * Copyright (c) YugaByte, Inc.
 */

 package preflight

 import (
	 "strconv"
	 "go.etcd.io/etcd/pkg/fileutil"
	 "strings"
	 "regexp"
	 "os"
  )
 
 var root = Root{"root", "warning"}
 
 type Root struct {
	 Name string
	 WarningLevel string
 }
 
 func (r Root) GetName() string {
	 return r.Name
 }
 
 func (r Root) GetWarningLevel() string {
	 return r.WarningLevel
 }
 
 func (r Root) Execute() {
 
	if _, existsErr := os.Stat(INSTALL_ROOT); existsErr == nil {
		err := fileutil.IsDirWriteable(INSTALL_ROOT)
		if err != nil {
			LogError(INSTALL_ROOT + " is not writeable.")
		} else {
			LogInfo(INSTALL_ROOT + " is writeable.")
		}
		command := "bash"
		args := []string{"-c", "df --output=avail -h \"" + INSTALL_ROOT+ "\" | tail -n 1"}
		output, err := ExecuteBashCommand(command, args)
		if err != nil {
			LogError(err.Error())
		} else {
			outputTrimmed := strings.ReplaceAll(strings.TrimSuffix(output, "\n"), " ", "")
			re := regexp.MustCompile("[0-9]+")
			freeSpaceArray := re.FindAllString(outputTrimmed, 1)
			freeSpaceString := freeSpaceArray[0]
			freeSpace, _ := strconv.Atoi(freeSpaceString)
			if freeSpace == 0 {
				LogError(INSTALL_ROOT + " does not have free space.")
			} else {
				LogInfo(INSTALL_ROOT + " has free space.")
			}
		}
	  }
 }