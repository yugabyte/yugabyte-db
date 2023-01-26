/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"os"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
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

	if viper.GetBool("postgres.useExisting.enabled") {
		args = append(args, "--db_username", viper.GetString("postgres.useExisting.username"))
		args = append(args, "--db_host", viper.GetString("postgres.useExisting.host"))
		args = append(args, "--db_port", viper.GetString("postgres.useExisting.port"))
		// TODO: modify yb platform backup sript to accept a custom password
	}

	if viper.GetBool("postgres.install.enabled") {
		args = append(args, "--db_username", "postgres")
		args = append(args, "--db_host", "localhost")
		args = append(args, "--db_port", viper.GetString("postgres.install.port"))
	}

	log.Info("Creating a backup of your Yugabyte Anywhere Installation.")
	common.RunBash(fileName, args)
}

// RestoreBackupScript calls the yb_platform_backup.sh script with the correct args.
// TODO: Version check is still disabled because of issues finding the path across all installs.
func RestoreBackupScript(inputPath string, destination string, skipRestart bool,
	verbose bool, plat Platform) {
	userName := viper.GetString("service_username")
	fileName := plat.backupScript()
	err := os.Chmod(fileName, 0777)
	if err != nil {
		log.Fatal(err.Error())
	} else {
		log.Debug("Restore Backup Script has now been given executable permissions.")
	}

	args := []string{"restore", "--input", inputPath,
		"--destination", destination, "--data_dir", destination, "--disable_version_check",
		"--yba_installer"}
	if skipRestart {
		args = append(args, "--skip_restart")
	}
	if verbose {
		args = append(args, "--verbose")
	}
	if common.HasSudoAccess() {
		args = append(args, "-u", userName, "-e", userName)
	} else {
		args = append(args, "-u", common.GetCurrentUser(), "-e", common.GetCurrentUser())
	}
	log.Info("Restoring a backup of your Yugabyte Anywhere Installation.")
	common.RunBash(fileName, args)

}

func createBackupCmd() *cobra.Command {
	var dataDir string
	var excludePrometheus bool
	var skipRestart bool
	var verbose bool

	createBackup := &cobra.Command{
		Use:   "createBackup outputPath",
		Short: "The createBackup command is used to take a backup of your Yugabyte Anywhere instance.",
		Long: `
    The createBackup command executes our yb_platform_backup.sh that creates a backup of your
    Yugabyte Anywhere instance. Executing this command requires that you create and specify the
    outputPath where you want the backup .tar.gz file to be stored as the first argument to
    createBackup.
    `,
		Args: cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {

			outputPath := args[0]

			if plat, ok := services["yb-platform"].(Platform); ok {
				CreateBackupScript(outputPath, dataDir, excludePrometheus, skipRestart, verbose, plat)
			} else {
				log.Fatal("Could not cast service to Platform struct.")
			}
		},
	}

	createBackup.Flags().StringVar(&dataDir, "data_dir", common.GetBaseInstall(),
		"data directory to be backed up")
	createBackup.Flags().BoolVar(&excludePrometheus, "exclude_prometheus", false,
		"exclude prometheus metric data from backup (default: false)")
	createBackup.Flags().BoolVar(&skipRestart, "skip_restart", false,
		"don't restart processes during execution (default: false)")
	createBackup.Flags().BoolVar(&verbose, "verbose", false,
		"verbose output of script (default: false)")
	return createBackup
}

func restoreBackupCmd() *cobra.Command {
	var destination string
	var skipRestart bool
	var verbose bool

	restoreBackup := &cobra.Command{
		Use:   "restoreBackup inputPath",
		Short: "The restoreBackup command restores a backup of your Yugabyte Anywhere instance.",
		Long: `
    The restoreBackup command executes our yb_platform_backup.sh that restores the backup of your
    Yugabyte Anywhere instance. Executing this command requires that you create and specify the
    inputPath where the backup .tar.gz file that will be restored is located as the first argument
    to restoreBackup.
    `,
		Args: cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {

			inputPath := args[0]

			// TODO: backupScript is the only reason we need to have this cast. Should probably refactor.
			if plat, ok := services["yb-platform"].(Platform); ok {
				RestoreBackupScript(inputPath, destination, skipRestart, verbose, plat)
			} else {
				log.Fatal("Could not cast service to Platform for backup script execution.")
			}

		},
	}

	restoreBackup.Flags().StringVar(&destination, "destination", common.GetBaseInstall(),
		"where to un-tar the backup")
	restoreBackup.Flags().BoolVar(&skipRestart, "skip_restart", false,
		"don't restart processes during execution (default: false)")
	restoreBackup.Flags().BoolVar(&verbose, "verbose", false,
		"verbose output of script (default: false)")
	return restoreBackup
}

func init() {
	// Backup commands must be run from installed yba-ctl
	if common.RunFromInstalled() {
		rootCmd.AddCommand(createBackupCmd(), restoreBackupCmd())
	}
}
