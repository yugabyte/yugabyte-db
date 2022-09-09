/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
	"os"
)

// CreateBackupScript execution method that directly calls backup.sh
// (the updated and maintained backup script). Am assuming that an updated backup.sh
// script will now be provided in the Platform support packages directory that
// is equivalent to yb_platform_backup.sh in managed.
func CreateBackupScript(outputPath string, dataDir string,
	excludePrometheus bool, skipRestart bool, verbose bool) {

	fileName := INSTALL_VERSION_DIR + "/packages/backup.sh"
	err := os.Chmod(fileName, 0777)
	if err != nil {
		LogError(err.Error())
	} else {
		LogDebug("Create Backup Script has now been given executable permissions.")
	}

	command1 := "/bin/sh"
	arg1 := []string{fileName, "backup", "--output", outputPath, "--data_dir", dataDir}
	if excludePrometheus {
		arg1 = append(arg1, "--exclude-prometheus")
	}
	if skipRestart {
		arg1 = append(arg1, "--skip_restart")
	}
	if verbose {
		arg1 = append(arg1, "--verbose")
	}
	LogInfo("Creating a backup of your Yugabyte Anywhere Installation.")
	ExecuteBashCommand(command1, arg1)
}
