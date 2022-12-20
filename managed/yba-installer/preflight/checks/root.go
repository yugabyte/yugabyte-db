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

// Root is the check if we can do a root based install
var Root = &rootCheck{"root", false}

type rootCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (r rootCheck) Name() string {
	return r.name
}

// SkipAllowed returns if we are allowed to skip the check
func (r rootCheck) SkipAllowed() bool {
	return r.skipAllowed
}

// Execute will run the root check.
// Validates the root install directory is not used and has enough free space??
func (r rootCheck) Execute() Result {
	res := Result{
		Check:  r.name,
		Status: StatusPassed,
	}
	if _, existsErr := os.Stat(common.GetInstallRoot()); existsErr == nil {
		err := fileutil.IsDirWriteable(common.GetInstallRoot())
		if err != nil {
			res.Error = fmt.Errorf(common.GetInstallRoot() + " is not writeable.")
			res.Status = StatusCritical
			return res
		} else {
			log.Info(common.GetInstallRoot() + " is writeable.")
		}
		command := "bash"
		// TODO: Also duplicated some df code in ssd.go.
		// Should resolve with https://yugabyte.atlassian.net/browse/PLAT-6177
		args := []string{"-c", "df --output=avail -h \"" + common.GetInstallRoot() + "\" | tail -n 1"}
		output, err := common.ExecuteBashCommand(command, args)
		if err != nil {
			res.Error = err
			res.Status = StatusCritical
			return res
		}

		outputTrimmed := strings.ReplaceAll(strings.TrimSuffix(output, "\n"), " ", "")
		re := regexp.MustCompile("[0-9]+")
		freeSpaceArray := re.FindAllString(outputTrimmed, 1)
		freeSpaceString := freeSpaceArray[0]
		freeSpace, _ := strconv.Atoi(freeSpaceString)
		if freeSpace == 0 {
			res.Error = fmt.Errorf(common.GetInstallRoot() + " does not have free space.")
			res.Status = StatusCritical
		} else {
			log.Info(common.GetInstallRoot() + " has free space.")
		}
	}
	return res
}
