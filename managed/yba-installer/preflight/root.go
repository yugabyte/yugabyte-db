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

	 log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	 "github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
  )

 var root = Root{"root", "warning"}

 type Root struct {
	 name string
	 WarningLevel string
 }

 func (r Root) Name() string {
	 return r.name
 }

 func (r Root) GetWarningLevel() string {
	 return r.WarningLevel
 }

 func (r Root) Execute() {

	if _, existsErr := os.Stat(common.InstallRoot); existsErr == nil {
		err := fileutil.IsDirWriteable(common.InstallRoot)
		if err != nil {
			log.Fatal(common.InstallRoot + " is not writeable.")
		} else {
			log.Info(common.InstallRoot + " is writeable.")
		}
		command := "bash"
		// TODO: Also duplicated some df code in ssd.go.
		// Should resolve with https://yugabyte.atlassian.net/browse/PLAT-6177
		args := []string{"-c", "df --output=avail -h \"" + common.InstallRoot+ "\" | tail -n 1"}
		output, err := common.ExecuteBashCommand(command, args)
		if err != nil {
			log.Fatal(err.Error())
		} else {
			outputTrimmed := strings.ReplaceAll(strings.TrimSuffix(output, "\n"), " ", "")
			re := regexp.MustCompile("[0-9]+")
			freeSpaceArray := re.FindAllString(outputTrimmed, 1)
			freeSpaceString := freeSpaceArray[0]
			freeSpace, _ := strconv.Atoi(freeSpaceString)
			if freeSpace == 0 {
				log.Fatal(common.InstallRoot + " does not have free space.")
			} else {
				log.Info(common.InstallRoot + " has free space.")
			}
		}
	  }
 }
