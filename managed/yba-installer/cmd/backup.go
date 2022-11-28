/*
 * Copyright (c) YugaByte, Inc.
 */

 package cmd

 import (
	 "os"
 )

 // CreateBackupScript calls the yb_platform_backup.sh script with the correct args.
 func CreateBackupScript(outputPath string, dataDir string,
	 excludePrometheus bool, skipRestart bool, verbose bool, plat Platform) {

	 fileName := plat.getBackupScript()
	 err := os.Chmod(fileName, 0777)
	 if err != nil {
		 LogError(err.Error())
	 } else {
		 LogDebug("Create Backup Script has now been given executable permissions.")
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
	 if hasSudoAccess() {
		args = append(args, "-u", "yugabyte")
	 } else {
		args = append(args, "-u", GetCurrentUser())
	 }
	 LogInfo("Creating a backup of your Yugabyte Anywhere Installation.")
	 ExecuteBashCommand(fileName, args)
 }

 // RestoreBackupScript calls the yb_platform_backup.sh script with the correct args.
 // TODO: Version check is still disabled because of issues finding the path across all installs.
 func RestoreBackupScript(inputPath string, destination string, skipRestart bool,
	 verbose bool, plat Platform) {

	 fileName := plat.getBackupScript()
	 err := os.Chmod(fileName, 0777)
	 if err != nil {
		 LogError(err.Error())
	 } else {
		 LogDebug("Restore Backup Script has now been given executable permissions.")
	 }

	 args := []string{"restore", "--input", inputPath,
										"--destination", destination, "--disable_version_check", "--yba_installer"}
	 if skipRestart {
		 args = append(args, "--skip_restart")
	 }
	 if verbose {
		 args = append(args, "--verbose")
	 }
	 if hasSudoAccess() {
		args = append(args, "-u", "yugabyte", "-e", "yugabyte")
	 } else {
		args = append(args, "-u", GetCurrentUser(), "-e", GetCurrentUser())
	 }
	 LogInfo("Restoring a backup of your Yugabyte Anywhere Installation.")
	 ExecuteBashCommand(fileName, args)
 }
