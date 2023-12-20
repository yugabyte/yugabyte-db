/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"database/sql"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/config"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// CreateBackupScript calls the yb_platform_backup.sh script with the correct args.
func CreateBackupScript(outputPath string, dataDir string,
	excludePrometheus bool, excludeReleases bool, skipRestart bool, verbose bool, plat Platform) {

	fileName := plat.backupScript()
	err := os.Chmod(fileName, 0777)
	if err != nil {
		log.Fatal(err.Error())
	} else {
		log.Debug("Create Backup Script has now been given executable permissions.")
	}

	args := []string{"create", "--output", outputPath, "--data_dir", dataDir, "--yba_installer"}
	if excludePrometheus {
		args = append(args, "--exclude_prometheus")
	}
	if excludeReleases {
		args = append(args, "--exclude_releases")
	}
	if skipRestart {
		args = append(args, "--skip_restart")
	}
	if verbose {
		args = append(args, "--verbose")
	}
	if viper.GetBool("ybdb.install.enabled") {
		args = append(args, "--ysql_dump_path", plat.YsqlDump)
		args = addYbdbArgs(args)
	} else if viper.GetBool("postgres.useExisting.enabled") {
		if viper.GetString("postgres.useExisting.pg_dump_path") != "" {
			args = append(args, "--pg_dump_path", viper.GetString("postgres.useExisting.pg_dump_path"))
			createPgPass()
			args = append(args, "--pgpass_path", common.PgpassPath())
			args = addPostgresArgs(args)
		} else {
			log.Fatal("pg_dump path must be set. Stopping backup process")
		}
	} else {
		args = append(args, "--pg_dump_path", plat.PgBin+"/pg_dump")
		args = addPostgresArgs(args)
	}

	log.Info("Creating a backup of your YugabyteDB Anywhere Installation.")
	out := shell.Run(fileName, args...)
	if !out.SucceededOrLog() {
		log.Fatal(out.Error.Error())
	}
}

// CreateReplicatedBackupScript backs up a replicated based installation of YBA.
func CreateReplicatedBackupScript(output, dataDir, pgUser, pgPort string, verbose bool,
	plat Platform) {
	fileName := plat.backupScript()
	err := os.Chmod(fileName, 0777)
	if err != nil {
		log.Fatal(err.Error())
	} else {
		log.Debug("Create Backup Script has now been given executable permissions.")
	}

	args := []string{"create", "--output", output, "--data_dir", dataDir, "--exclude_prometheus",
		"--exclude_releases", "--disable_version_check", "--db_username", pgUser,
		"--db_host", "localhost", "--db_port", pgPort}

	if verbose {
		args = append(args, "--verbose")
	}

	log.Info("Creating a backup of your Replicated YBA Installation.")
	out := shell.Run(fileName, args...)
	if !out.SucceededOrLog() {
		log.Fatal(out.Error.Error())
	}

}

// RestoreBackupScript calls the yb_platform_backup.sh script with the correct args.
// TODO: Version check is still disabled because of issues finding the path across all installs.
func RestoreBackupScript(inputPath string, destination string, skipRestart bool,
	verbose bool, plat Platform, migration bool, useSystemPostgres bool) {
	userName := viper.GetString("service_username")
	fileName := plat.backupScript()
	err := os.Chmod(fileName, 0777)
	if err != nil {
		log.Fatal(err.Error())
	} else {
		log.Debug("Restore Backup Script has now been given executable permissions.")
	}

	args := []string{"restore", "--input", inputPath,
		"--destination", destination, "--data_dir", destination, "--yba_installer",
		"--yba_user", userName, "--ybai_data_dir", plat.DataDir}
	if skipRestart {
		args = append(args, "--skip_restart")
	}
	if migration {
		args = append(args, "--migration")
		// Disable version checking in case of version upgrades during migration.
		args = append(args, "--disable_version_check")
	}
	if useSystemPostgres {
		args = append(args, "--use_system_pg")
	}
	if verbose {
		args = append(args, "--verbose")
	}
	// Add prometheus user
	if common.HasSudoAccess() {
		args = append(args, "-e", userName)
	} else {
		args = append(args, "-e", common.GetCurrentUser())
	}

	if viper.GetBool("ybdb.install.enabled") {
		args = append(args, "--ysqlsh_path", plat.YsqlBin)
		args = addYbdbArgs(args)
	} else if viper.GetBool("postgres.useExisting.enabled") {
		if viper.GetString("postgres.useExisting.pg_restore_path") != "" {
			args = append(args, "--pg_restore_path", viper.GetString(
				"postgres.useExisting.pg_restore_path"))
			if viper.GetString("postgress.useExisting.password") != "" {
				createPgPass()
				args = append(args, "--pgpass_path", common.PgpassPath())
			}
			args = addPostgresArgs(args)
		} else {
			log.Fatal("pg_restore path must be set. Stopping restore process.")
		}
	} else {
		args = append(args, "--pg_restore_path", plat.PgBin+"/pg_restore")
		args = addPostgresArgs(args)
	}
	log.Info("Restoring a backup of your YugabyteDB Anywhere Installation.")
	if out := shell.Run(fileName, args...); !out.SucceededOrLog() {
		log.Fatal("Restore script failed. May need to restart services.")
	}
	if common.HasSudoAccess() {
		log.Debug("ensuring ownership of restored directories")
		user := viper.GetString("service_user")
		if err := common.Chown(plat.DataDir, user, user, true); err != nil {
			log.Fatal("failed to change ownership of " + plat.DataDir + "to user/group " + user)
		}
	}

	if err := plat.SetDataDirPerms(); err != nil {
		log.Warn(fmt.Sprintf("Could not set %s permissions.", plat.DataDir))
	}

	if migration {
		// Wait a minute so that files are found on filesystem
		time.Sleep(15 * time.Second)
		// set fixPaths conf variable
		plat.FixPaths = true
		config.GenerateTemplate(plat)

		if err := plat.Restart(); err != nil {
			log.Fatal(fmt.Sprintf("Error %s restarting yb-platform.", err.Error()))
		}
	}
}

func addPostgresArgs(args []string) []string {
	if viper.GetBool("postgres.useExisting.enabled") {
		args = append(args, "--db_username", viper.GetString("postgres.useExisting.username"))
		args = append(args, "--db_host", viper.GetString("postgres.useExisting.host"))
		args = append(args, "--db_port", viper.GetString("postgres.useExisting.port"))
		// TODO: modify yb platform backup sript to accept a custom password
	}

	if viper.GetBool("postgres.install.enabled") {
		// TODO: change to postgres.install.username when it merges
		args = append(args, "--db_username", "postgres")
		args = append(args, "--db_host", "localhost")
		args = append(args, "--db_port", viper.GetString("postgres.install.port"))
	}
	return args
}

func addYbdbArgs(args []string) []string {
	args = append(args, "--db_username", "yugabyte")
	args = append(args, "--db_host", "localhost")
	args = append(args, "--db_port", viper.GetString("ybdb.install.port"))
	args = append(args, "--ybdb")

	return args
}

func createPgPass() {
	data := []byte(fmt.Sprintf("%s:%s:yugaware:%s:%s",
		viper.GetString("postgres.useExisting.host"),
		viper.GetString("postgres.useExisting.port"),
		viper.GetString("postgres.useExisting.username"),
		viper.GetString("postgres.useExisting.password"),
	))
	err := os.WriteFile(common.PgpassPath(), data, 0600)
	if err != nil {
		log.Fatal("could not create pgpass file: " + err.Error())
	}
}

func createBackupCmd() *cobra.Command {
	var dataDir string
	var excludePrometheus bool
	var excludeReleases bool
	var skipRestart bool
	var verbose bool

	createBackup := &cobra.Command{
		Use:   "createBackup outputPath",
		Short: "The createBackup command is used to take a backup of your YugabyteDB Anywhere instance.",
		Long: `
    The createBackup command executes our yb_platform_backup.sh that creates a backup of your
    YugabyteDB Anywhere instance. Executing this command requires that you specify the
    outputPath where you want the backup .tar.gz file to be stored as the first argument to
    createBackup.
    `,
		Args: cobra.ExactArgs(1),
		PreRun: func(cmd *cobra.Command, args []string) {
			if !common.RunFromInstalled() {
				path := filepath.Join(common.YbactlInstallDir(), "yba-ctl")
				log.Fatal("createBackup must be run from " + path +
					". It may be in the systems $PATH for easy of use.")
			}
		},
		Run: func(cmd *cobra.Command, args []string) {

			outputPath := args[0]
			if plat, ok := services["yb-platform"].(Platform); ok {
				CreateBackupScript(outputPath, dataDir, excludePrometheus, excludeReleases, skipRestart,
					verbose, plat)
			} else {
				log.Fatal("Could not cast service to Platform struct.")
			}
		},
	}

	createBackup.Flags().StringVar(&dataDir, "data_dir", common.GetBaseInstall(),
		"data directory to be backed up")
	createBackup.Flags().BoolVar(&excludePrometheus, "exclude_prometheus", false,
		"exclude prometheus metric data from backup (default: false)")
	createBackup.Flags().BoolVar(&excludeReleases, "exclude_releases", false,
		"exclude YBDB releases from backup (default: false)")
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
	var migration bool
	var useSystemPostgres bool
	var skipYugawareDrop bool

	restoreBackup := &cobra.Command{
		Use:   "restoreBackup inputPath",
		Short: "The restoreBackup command restores a backup of your YugabyteDB Anywhere instance.",
		Long: `
    The restoreBackup command executes our yb_platform_backup.sh that restores from a previously
		taken backup of your YugabyteDB Anywhere instance. Executing this command requires that you
		specify the inputPath to the backup .tar.gz file as the only argument to restoreBackup.
    `,
		Args: cobra.ExactArgs(1),
		PreRun: func(cmd *cobra.Command, args []string) {
			if !common.RunFromInstalled() {
				path := filepath.Join(common.YbactlInstallDir(), "yba-ctl")
				log.Fatal("restoreBackup must be run from " + path +
					". It may be in the systems $PATH for easy of use.")
			}
		},
		Run: func(cmd *cobra.Command, args []string) {

			inputPath := args[0]

			// TODO: backupScript is the only reason we need to have this cast. Should probably refactor.
			if plat, ok := services["yb-platform"].(Platform); ok {
				// Drop the yugaware database.
				if migration && !skipYugawareDrop {
					prompt := "Restoring previous YBA will drop the existing yugaware database. Continue?"
					if !common.UserConfirm(prompt, common.DefaultYes) {
						log.Fatal("Stopping migration restore.")
					}
					if err := plat.Stop(); err != nil {
						log.Warn(fmt.Sprintf(
							"Error %s stopping yb-platform. Continuing with migration restore.", err.Error()))
					}
					var db *sql.DB
					var connStr string
					var err error
					if viper.GetBool("postgres.useExisting.enabled") {
						db, connStr, err = common.GetPostgresConnection(
							viper.GetString("postgres.useExisting.username"))
					} else {
						db, connStr, err = common.GetPostgresConnection(viper.GetString("postgres.install.username"))
					}
					if err != nil {
						log.Fatal(fmt.Sprintf(
							"Can't connect to postgres DB with connection string: %s. Error: %s",
							connStr, err.Error()))
					}
					_, err = db.Query("DROP DATABASE yugaware;")
					if err != nil {
						log.Fatal(fmt.Sprintf("Error %s trying to drop yugaware DB.", err.Error()))
					}
					_, err = db.Query("CREATE DATABASE yugaware;")
					if err != nil {
						log.Fatal(fmt.Sprintf("Error %s trying to create yugaware DB.", err.Error()))
					}
				}
				RestoreBackupScript(inputPath, destination, skipRestart, verbose, plat, migration,
					useSystemPostgres)

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
	restoreBackup.Flags().BoolVar(&migration, "migration", false,
		"restoring from a Replicated or Yugabundle installation (default: false)")
	restoreBackup.Flags().BoolVar(&migration, "yugabundle", false,
		"WARNING: yugabundle flag is deprecated.\n"+
			"Please use migration instead to migrate from yugabundle to YBA-installer. (default: false)")
	restoreBackup.MarkFlagsMutuallyExclusive("migration", "yugabundle")
	restoreBackup.Flags().BoolVar(&useSystemPostgres, "use_system_pg", false,
		"use system path's pg_restore as opposed to installed binary (default: false)")
	restoreBackup.Flags().BoolVar(&skipYugawareDrop, "skip_dbdrop", false,
		"skip dropping the yugaware database before a migration restore (default: false)")
	return restoreBackup
}

func init() {
	rootCmd.AddCommand(createBackupCmd(), restoreBackupCmd())
}
