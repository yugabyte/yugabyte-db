/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"os"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
)

 // CreateBackupScript calls the yb_platform_backup.sh script with the correct args.
func CreateBackupScript(outputPath string, dataDir string,
	excludePrometheus bool, skipRestart bool, verbose bool, plat Platform) {

	fileName := plat.backupScript()
	err := os.Chmod(fileName, 0777)
	if err != nil {
		log.Fatal(err.Error())
	} else {
		log.Debug("Create Backup Script has now been given executable permissions.")
	}

	args := []string{"create", "--output", outputPath, "--data_dir", dataDir, "--yba_installer"}
	if excludePrometheus {
		args = append(args, "--exclude-prometheus")
	}
	if skipRestart {
		args = append(args, "--skip_restart")
	}
	if verbose {
		args = append(args, "--verbose")
	}
	if common.HasSudoAccess() {
	args = append(args, "-u", "yugabyte")
	} else {
	args = append(args, "-u", common.GetCurrentUser())
	}
	log.Info("Creating a backup of your Yugabyte Anywhere Installation.")
	common.ExecuteBashCommand(fileName, args)
 }

 // RestoreBackupScript calls the yb_platform_backup.sh script with the correct args.
 // TODO: Version check is still disabled because of issues finding the path across all installs.
func RestoreBackupScript(inputPath string, destination string, skipRestart bool,
	verbose bool, plat Platform) {

	fileName := plat.backupScript()
	err := os.Chmod(fileName, 0777)
	if err != nil {
		log.Fatal(err.Error())
	} else {
		log.Debug("Restore Backup Script has now been given executable permissions.")
	}

	args := []string{"restore", "--input", inputPath,
									"--destination", destination, "--disable_version_check", "--yba_installer"}
	if skipRestart {
		args = append(args, "--skip_restart")
	}
	if verbose {
		args = append(args, "--verbose")
	}
	if common.HasSudoAccess() {
	args = append(args, "-u", "yugabyte", "-e", "yugabyte")
	} else {
	args = append(args, "-u", common.GetCurrentUser(), "-e", common.GetCurrentUser())
	}
	log.Info("Restoring a backup of your Yugabyte Anywhere Installation.")
	common.ExecuteBashCommand(fileName, args)

}
