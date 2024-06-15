/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"os"

	"go.etcd.io/etcd/client/pkg/v3/fileutil"
	"golang.org/x/sys/unix"

	"github.com/dustin/go-humanize"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

const minFreeDiskInstall uint64 = 200 * 1024 * 1024 * 1024 // 200 GB

const minFreeDiskUpgrade uint64 = 25 * 1024 * 1024 * 1024 // 25 GB

var DiskAvail = &diskAvailCheck{"disk-availability", true}

type diskAvailCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (r diskAvailCheck) Name() string {
	return r.name
}

// SkipAllowed returns if we are allowed to skip the check
func (r diskAvailCheck) SkipAllowed() bool {
	return r.skipAllowed
}

// Execute will run the root check.
// Validates the root install directory is not used and has enough free space??
func (r diskAvailCheck) Execute() Result {
	res := Result{
		Check:  r.name,
		Status: StatusPassed,
	}

	baseDir := common.GetBaseInstall()
	bytesAvail, err := getFreeBytes(baseDir)
	if err != nil {
		res.Error = err
		res.Status = StatusCritical
		return res
	}

	minDiskReq := minFreeDiskInstall
	_, err = os.Stat(common.YbaInstalledMarker())
	if err == nil {
		// upgrade
		minDiskReq = minFreeDiskUpgrade
	}
	if bytesAvail < minDiskReq {
		res.Error = fmt.Errorf(
			"Available disk space on volume %s is less than minimum required %s",
			humanize.IBytes(bytesAvail), humanize.IBytes(minDiskReq))
		res.Status = StatusCritical
		return res
	}

	return res
}

func getFreeBytes(path string) (uint64, error) {
	if _, existsErr := os.Stat(path); existsErr == nil {
		err := fileutil.IsDirWriteable(path)
		if err != nil {
			return 0, fmt.Errorf(path + " is not writeable.")
		}
		log.Debug(path + " is writeable.")
	}

	// walk up the dir path until we find a dir that exists
	validParentDir, err := common.GetValidParent(path)
	if err != nil {
		return 0, fmt.Errorf("No valid parent dir for install dir " + path)
	}

	var stat unix.Statfs_t
	err = unix.Statfs(validParentDir, &stat)
	if err != nil {
		return 0, fmt.Errorf("Cannot read disk availability of " + validParentDir)
	}
	return stat.Bavail * uint64(stat.Bsize), nil
}
