/*
 * Copyright (c) YugaByte, Inc.
 */

package checks

import (
	"fmt"
	"os"

	"go.etcd.io/etcd/pkg/fileutil"
	"golang.org/x/sys/unix"

	"github.com/dustin/go-humanize"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
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

	if _, existsErr := os.Stat(baseDir); existsErr == nil {
		err := fileutil.IsDirWriteable(baseDir)
		if err != nil {
			res.Error = fmt.Errorf(baseDir + " is not writeable.")
			res.Status = StatusCritical
			return res
		}
		log.Debug(baseDir + " is writeable.")
	}

	// walk up the dir path until we find a dir that exists
	validParentDir, err := common.GetValidParent(baseDir)
	if err != nil {
		log.Fatal("No valid parent dir for install dir " + baseDir)
	}
	var stat unix.Statfs_t
	err = unix.Statfs(validParentDir, &stat)
	if err != nil {
		res.Error = fmt.Errorf("Cannot read disk availability of " + validParentDir)
		res.Status = StatusCritical
		return res
	}

	minDiskReq := minFreeDiskInstall
	_, err = os.Stat(common.InstalledFile)
	if err == nil {
		// upgrade
		minDiskReq = minFreeDiskUpgrade
	}
	bytesAvail := stat.Bavail * uint64(stat.Bsize)
	if bytesAvail < minDiskReq {
		res.Error = fmt.Errorf(
			"Availabile disk space on volume %s is less than minimum required %s",
			humanize.Bytes(bytesAvail), humanize.Bytes(minDiskReq))
		res.Status = StatusCritical
		return res
	}

	return res
}
