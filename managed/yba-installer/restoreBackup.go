/*
 * Copyright (c) YugaByte, Inc.
 */

package main

import (
	"os"
)

// RestoreBackup Script execution method that directly calls backup.sh
// (the updated and maintained backup script).

// Am assuming that an updated backup.sh script will now be provided
// in the Platform support packages directory that is equivalent to
// yb_platform_backup.sh in managed.
func RestoreBackupScript(input_path string, destination string, skip_restart bool,
	verbose bool) {

	fileName := INSTALL_VERSION_DIR + "/packages/backup.sh"
	err := os.Chmod(fileName, 0777)
	if err != nil {
		LogError(err.Error())
	} else {
		LogDebug("Restore Backup Script has now been given executable permissions.")
	}

	command1 := "/bin/sh"
	arg1 := []string{fileName, "restore", "--input", input_path, "--destination", destination}
	if skip_restart {
		arg1 = append(arg1, "--skip_restart")
	}
	if verbose {
		arg1 = append(arg1, "--verbose")
	}
	LogInfo("Restoring a backup of your Yugabyte Anywhere Installation.")
	ExecuteBashCommand(command1, arg1)
}
