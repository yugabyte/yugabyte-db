package cmd

import (
	"fmt"
	"path/filepath"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/ybactl"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/yugaware"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

// rollback function is best effort and will not throw any errors
func rollbackUpgrade(backupDir string, state *ybactlstate.State) {
	log.Warn("Error encountered during upgrade, rolling back to previously installed YBA version.")
	// Restore YBA data
	if backupDir != "" {
		backup := common.FindRecentBackup(backupDir)
		log.Info(fmt.Sprintf("Rolling YBA data back from %s", backup))
		RestoreBackupScriptHelper(backup, common.GetBaseInstall(), true, true, false, false, true,
			fmt.Sprintf("%s/yba_installer/packages/yugabyte-%s/devops/bin/yb_platform_backup.sh",
				common.GetActiveSymlink(), state.Version),
			common.GetBaseInstall() + "/data/yb-platform",
			common.GetActiveSymlink() + "/ybdb/bin/ysqlsh",
			common.GetActiveSymlink() + "/pgsql/bin/pg_restore")
	}

	// Validate symlink
	activePath, err := filepath.EvalSymlinks(common.GetActiveSymlink())
	if err != nil {
		log.Fatal(err.Error())
	}
	activePathBase := filepath.Base(activePath)
	if state.Version != activePathBase {
		log.Fatal("the active symlink is not pointing to the current installed version, aborting rollback")
	}

	// Copy back over correct yba-ctl
	findArgs := []string{common.GetActiveSymlink() + "/", "-name", "yba-ctl", "-exec", "cp", "{}",
		common.YbactlInstallDir(), ";"}
	if out := shell.Run("find", findArgs...); !out.SucceededOrLog() {
		log.Warn(fmt.Sprintf("failed to reinstall yba-ctl: %w", out.Error))
	}

	// Remove newest install
	common.RemoveAll(common.GetSoftwareRoot())

	// Reset Status back to installed, the version has not yet been updated
	state.CurrentStatus = ybactlstate.InstalledStatus
	if err := ybactlstate.StoreState(state); err != nil {
		log.Warn("failed to write state back to installed: " + err.Error())
	}

	// reconfigure with the old binary
	if out:= shell.Run(filepath.Join(common.YbactlInstallDir(), "yba-ctl"), "reconfigure"); !out.SucceededOrLog() {
		log.Warn(fmt.Sprintf("failed to reconfigure with old yba version: %w", out.Error))
	}

	// cleanup old backups
	if err := common.KeepMostRecentFiles(backupDir, common.BackupRegex, 2); err != nil {
		log.Warn("error cleaning up " + backupDir)
	}
}

func upgradeCmd() *cobra.Command {

	var rollback = true

	var upgradeCmd = &cobra.Command{
		Use:   "upgrade",
		Short: "Upgrade an existing YugabyteDB Anywhere installation.",
		Long: `
   	The upgrade command will upgrade an already installed version of Yugabyte Anywhere to the
	 	upgrade version associated with your new download of YBA Installer. Please make sure that you
	 	have installed YugabyteDB Anywhere using the install command prior to executing the upgrade
	 	command.`,
		Args: cobra.NoArgs,
		// We will use prerun to do some basic setup for the upcoming upgrade.
		// At this point, its making sure Directory Manager is set to do an upgrade.
		PreRun: func(cmd *cobra.Command, args []string) {
			// TODO: IMO this is error prone - as in it can be easy to forget we need
			// to change the directory manager workflow. In the future, I think we should have
			// some sort of config that is given to all structs we create, and based on that be able to
			// chose the correct workflow.
			common.SetWorkflowUpgrade()

			if common.RunFromInstalled() {
				log.Fatal("Upgrade must be executed from the target yba bundle, not the existing install")
			}

			if !skipVersionChecks {
				installedVersion, err := yugaware.InstalledVersionFromMetadata()
				if err != nil {
					log.Fatal("Cannot upgrade: " + err.Error())
				}
				targetVersion := ybactl.Version
				if !common.LessVersions(installedVersion, targetVersion) {
					log.Fatal(fmt.Sprintf("upgrade target version '%s' must be greater then the installed "+
						"YugabyteDB Anywhere version '%s'", targetVersion, installedVersion))
				}
			}
		},
		Run: func(cmd *cobra.Command, args []string) {
			state, err := ybactlstate.Initialize()
			// Can have no state if upgrading from a version before state existed.
			if err != nil {
				state = ybactlstate.New()
				state.CurrentStatus = ybactlstate.InstalledStatus
			}

			if err := state.TransitionStatus(ybactlstate.UpgradingStatus); err != nil {
				log.Fatal("cannot upgrade, invalid status transition: " + err.Error())
			}

			if err := state.ValidateReconfig(); err != nil {
				log.Fatal("invalid reconfigure during upgrade: " + err.Error())
			}

			// Upgrade yba-ctl first.
			if err := ybaCtl.Install(); err != nil {
				log.Fatal("failed to upgrade yba-ctl")
			}

			//Todo: this is a temporary hidden feature to migrate data
			//from Pg to Ybdb and vice-a-versa.
			results := preflight.Run(preflight.UpgradeChecks, skippedPreflightChecks...)
			if preflight.ShouldFail(results) {
				preflight.PrintPreflightResults(results)
				log.Fatal("preflight failed")
			}

			// Take a backup of YBA as a safety measure
			backupDir := filepath.Join(common.GetDataRoot(), "upgradeYbaBackup")
			if err := common.MkdirAll(backupDir, common.DirMode); err == nil {
				log.Info(fmt.Sprintf("Taking YBA backup to %s", backupDir))
				if errB := CreateBackupScriptHelper(backupDir, common.GetBaseInstall(), true, true, false, true, false,
					fmt.Sprintf("%s/yba_installer/packages/yugabyte-%s/devops/bin/yb_platform_backup.sh", common.GetActiveSymlink(), state.Version),
					common.GetActiveSymlink() + "/ybdb/postgres/bin/ysql_dump",
					common.GetActiveSymlink() + "/pgsql/bin/pg_dump"); errB != nil {
						if rollback{
							rollbackUpgrade("", state)
						}
						log.Fatal("Failed taking backup of YBA")
					}
			}

			/* This is the postgres major version upgrade workflow!
			// First, stop platform and prometheus. Postgres will need to be running
			// to take the backup for postgres upgrade.
			services[YbPlatformServiceName].Stop()
			services[PrometheusServiceName].Stop()

			common.Upgrade(ybactl.Version)

			for _, name := range serviceOrder {
				services[name].Upgrade()
			}

			for _, name := range serviceOrder {
				status := services[name].Status()
				if !common.IsHappyStatus(status) {
					log.Fatal(status.Service + " is not running! upgrade failed")
				}
			}
			*/

			// Here is the postgres minor version/no upgrade workflow
			if err := common.Upgrade(ybactl.Version); err != nil {
				if rollback {
					rollbackUpgrade(backupDir, state)
				}
				log.Fatal(fmt.Sprintf("Error performing common upgrade work: %s", err.Error()))
			}

			// Check if upgrading requires DB migration.

			/*dbMigrateFlow := state.GetDbUpgradeWorkFlow()

			var newDbServiceName string
			if dbMigrateFlow == ybactlstate.PgToYbdb {
				serviceOrder = serviceOrder[1:]
				migratePgToYbdbOrFatal()
				newDbServiceName = YbdbServiceName
				state.Postgres.IsEnabled = false
				state.Ybdb.IsEnabled = true
			} else if dbMigrateFlow == ybactlstate.YbdbToPg {
				serviceOrder = serviceOrder[1:]
				migrateYbdbToPgOrFatal()
				newDbServiceName = PostgresServiceName
				state.Postgres.IsEnabled = true
				state.Ybdb.IsEnabled = false
			}
			*/

			for _, name := range serviceOrder {
				log.Info("About to upgrade component " + name)
				if err := services[name].Upgrade(); err != nil {
					if rollback {
						rollbackUpgrade(backupDir, state)
					}
					log.Fatal("Upgrade of " + name + " failed: " + err.Error())
				}
				log.Info("Completed upgrade of component " + name)
			}

			for _, name := range serviceOrder {
				log.Info("About to restart component " + name)
				if err := services[name].Restart(); err != nil {
					if rollback {
						rollbackUpgrade(backupDir, state)
					}
					log.Fatal("Failed restarting " + name + " after upgrade: " + err.Error())
				}
				log.Info("Completed restart of component " + name)
			}

			if err := common.WaitForYBAReady(ybactl.Version); err != nil {
				if rollback {
					rollbackUpgrade(backupDir, state)
				}
				log.Fatal(fmt.Sprintf("Error waiting for YBA to respond ready: %s", err.Error()))
			}

			var statuses []common.Status
			//serviceOrder = append([]string{newDbServiceName}, serviceOrder...)
			for _, name := range serviceOrder {
				service := services[name]
				status, err := service.Status()
				if err != nil {
					log.Fatal("Failed to get status: " + err.Error())
				}
				statuses = append(statuses, status)
				if !common.IsHappyStatus(status) {
					if rollback {
						rollbackUpgrade(backupDir, state)
					}
					log.Fatal(status.Service + " is not running! upgrade failed")
				}
			}
			common.PrintStatus(statuses...)
			// Here ends the postgres minor version/no upgrade workflow

			state.CurrentStatus = ybactlstate.InstalledStatus
			state.Version = ybactl.Version
			if err := ybactlstate.StoreState(state); err != nil {
				log.Fatal("failed to write state: " + err.Error())
			}
			common.PostUpgrade()
		},
	}

	upgradeCmd.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip by name")
	upgradeCmd.Flags().BoolVarP(&rollback, "rollback", "r", true,
		"automatically rollback upgrade in case of errors (default: true)")
	return upgradeCmd
}

func init() {
	// Upgrade can only be run from the new version, not from the installed path
	rootCmd.AddCommand(upgradeCmd())
}
