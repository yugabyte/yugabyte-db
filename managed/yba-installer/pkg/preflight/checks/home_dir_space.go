/*
 * Copyright (c) YugabyteDB, Inc.
 */

package checks

import (
	"fmt"
	osuser "os/user"

	"github.com/dustin/go-humanize"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

const minFreeHomeDirSpace uint64 = 50 * 1024 * 1024 // 50 MB

var HomeDirSpace = &homeDirSpaceCheck{"home-directory-space", true}

type homeDirSpaceCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (h homeDirSpaceCheck) Name() string {
	return h.name
}

// SkipAllowed returns if we are allowed to skip the check
func (h homeDirSpaceCheck) SkipAllowed() bool {
	return h.skipAllowed
}

// Execute will run the home directory space check.
// Validates that the service user's home directory has at least 50MB of free space.
func (h homeDirSpaceCheck) Execute() Result {
	res := Result{
		Check:  h.name,
		Status: StatusPassed,
	}

	serviceUsername := viper.GetString("service_username")
	if serviceUsername == "" {
		// If service_username is not set, use the default
		serviceUsername = common.DefaultServiceUser
	}

	log.Debug("Checking home directory space for user: " + serviceUsername)

	// Look up the user to get their home directory
	user, err := osuser.Lookup(serviceUsername)
	if err != nil {
		res.Error = fmt.Errorf("failed to look up user '%s': %w", serviceUsername, err)
		res.Status = StatusWarning
		return res
	}

	homeDir := user.HomeDir
	if homeDir == "" {
		res.Error = fmt.Errorf("user '%s' does not have a home directory configured", serviceUsername)
		res.Status = StatusCritical
		return res
	}

	log.Debug("Checking available space in home directory: " + homeDir)

	// Check available space in the home directory
	bytesAvail, err := getFreeBytes(homeDir)
	if err != nil {
		res.Error = fmt.Errorf("failed to check available space in home directory '%s': %w", homeDir, err)
		res.Status = StatusCritical
		return res
	}

	if bytesAvail < minFreeHomeDirSpace {
		res.Error = fmt.Errorf(
			"Available disk space in home directory %s (%s) is less than minimum required %s",
			homeDir, humanize.IBytes(bytesAvail), humanize.IBytes(minFreeHomeDirSpace))
		res.Status = StatusCritical
		return res
	}

	log.Debug(fmt.Sprintf("Home directory %s has %s available space", homeDir, humanize.IBytes(bytesAvail)))
	return res
}
