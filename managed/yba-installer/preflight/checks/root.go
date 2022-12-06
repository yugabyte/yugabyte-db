/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"os"
	"regexp"
	"strconv"
	"strings"

	"go.etcd.io/etcd/pkg/fileutil"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var Root = &rootCheck{"root", "warning"}

type rootCheck struct {
	name         string
	warningLevel string
}

func (r rootCheck) Name() string {
	return r.name
}

func (r rootCheck) WarningLevel() string {
	return r.warningLevel
}

func (r rootCheck) Execute() error {

	if _, existsErr := os.Stat(common.InstallRoot); existsErr == nil {
		err := fileutil.IsDirWriteable(common.InstallRoot)
		if err != nil {
			return fmt.Errorf(common.InstallRoot + " is not writeable.")
		} else {
			log.Info(common.InstallRoot + " is writeable.")
		}
		command := "bash"
		// TODO: Also duplicated some df code in ssd.go.
		// Should resolve with https://yugabyte.atlassian.net/browse/PLAT-6177
		args := []string{"-c", "df --output=avail -h \"" + common.InstallRoot + "\" | tail -n 1"}
		output, err := common.ExecuteBashCommand(command, args)
		if err != nil {
			return err
		}

		outputTrimmed := strings.ReplaceAll(strings.TrimSuffix(output, "\n"), " ", "")
		re := regexp.MustCompile("[0-9]+")
		freeSpaceArray := re.FindAllString(outputTrimmed, 1)
		freeSpaceString := freeSpaceArray[0]
		freeSpace, _ := strconv.Atoi(freeSpaceString)
		if freeSpace == 0 {
			return fmt.Errorf(common.InstallRoot + " does not have free space.")
		} else {
			log.Info(common.InstallRoot + " has free space.")
		}
	}
	return nil
}
