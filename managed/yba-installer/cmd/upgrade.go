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
	// copy back yba-ctl.yml and .yba_installer.state
	common.CopyFile(filepath.Join(backupDir, common.InputFileName), common.InputFile())
	common.CopyFile(filepath.Join(backupDir, ybactlstate.StateFileName), filepath.Join(common.YbactlInstallDir(), ybactlstate.StateFileName))

	// reconfigure with the old binary
	if out := shell.Run(filepath.Join(common.YbactlInstallDir(), "yba-ctl"), "reconfigure"); !out.SucceededOrLog() {
		log.Warn(fmt.Sprintf("failed to reconfigure with old yba version: %s", out.Error.Error()))
	}

	// Stop YBA to prevent any potential schema changes PLAT-16051
	if out := shell.Run(filepath.Join(common.YbactlInstallDir(), "yba-ctl"), "stop", "yb-platform"); !out.SucceededOrLog() {
		log.Warn(fmt.Sprintf("failed to stop yba: %s", out.Error.Error()))
	}

	// Restore YBA data
	if backupDir != "" {
		backup := common.FindRecentBackup(backupDir)
		log.Info(fmt.Sprintf("Rolling YBA data back from %s", backup))
		err := RestoreBackupScriptHelper(backup, common.GetBaseInstall(), true, true, false, false, true,
			fmt.Sprintf("%s/yba_installer/packages/yugabyte-%s/devops/bin/yb_platform_backup.sh",
				common.GetActiveSymlink(), state.Version),
			common.GetBaseInstall()+"/data/yb-platform",
			common.GetActiveSymlink()+"/ybdb/bin/ysqlsh",
			common.GetActiveSymlink()+"/pgsql/bin/pg_restore")
		if err != nil {
			log.Warn(fmt.Sprintf("failed to restore backup: %s", err.Error()))
		}
	}

	// Start old YBA up again
	if out := shell.Run(filepath.Join(common.YbactlInstallDir(), "yba-ctl"), "start", "yb-platform"); !out.SucceededOrLog() {
		log.Warn(fmt.Sprintf("failed to stop yba: %s", out.Error.Error()))
	}

	// Remove newest install
	common.RemoveAll(common.GetSoftwareRoot())

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
			backupDir := filepath.Join(common.GetDataRoot(), "upgradeYbaBackup")
			if rollback {
				if err := common.MkdirAll(backupDir, common.DirMode); err != nil {
					log.Fatal(fmt.Sprintf("failed to create backup directory: %s", err.Error()))
				}
				// take backup of yba-ctl.yml and .yba_installer.state
				common.CopyFile(common.InputFile(), filepath.Join(backupDir, common.InputFileName))
				common.CopyFile(filepath.Join(common.YbactlInstallDir(), ybactlstate.StateFileName), filepath.Join(backupDir, ybactlstate.StateFileName))
			}
			state, err := ybactlstate.Initialize()
			// Can have no state if upgrading from a version before state existed.
			if err != nil {
				log.Fatal(fmt.Sprintf("Failed to initialize state: %v", err))
			}
			// Can have no state if upgrading from a version before state existed.
			if state.CurrentStatus == ybactlstate.UninstalledStatus {
				log.Warn("No state file found, assuming upgrade is from a version before state existed.")
				state.CurrentStatus = ybactlstate.InstalledStatus
			}

			if err := state.ValidateReconfig(); err != nil {
				log.Fatal("invalid reconfigure during upgrade: " + err.Error())
			}
			log.Info("Current state: " + state.Version)

			//Todo: this is a temporary hidden feature to migrate data
			//from Pg to Ybdb and vice-a-versa.
			results := preflight.Run(preflight.UpgradeChecks, skippedPreflightChecks...)
			if preflight.ShouldFail(results) {
				preflight.PrintPreflightResults(results)
				log.Fatal("preflight failed")
			}

			// Take a backup of YBA as a safety measure
			if rollback {
				log.Info(fmt.Sprintf("Taking YBA backup to %s", backupDir))
				usePromProtocol := true
				// PLAT-14522 introduced prometheus_protocol which isn't present in <2.20.7.0-b40 or <2024.1.3.0-b55
				if common.LessVersions(state.Version, "2.20.7.0-b40") ||
					(common.LessVersions("2024.1.0.0-b0", state.Version) && common.LessVersions(state.Version, "2024.1.3.0-b55")) {
					usePromProtocol = false
				}
				if errB := CreateBackupScriptHelper(backupDir, common.GetBaseInstall(),
					fmt.Sprintf("%s/yba_installer/packages/yugabyte-%s/devops/bin/yb_platform_backup.sh", common.GetActiveSymlink(), state.Version),
					common.GetActiveSymlink()+"/ybdb/postgres/bin/ysql_dump",
					common.GetActiveSymlink()+"/pgsql/bin/pg_dump",
					true, true, false, true, false, usePromProtocol); errB != nil {
					log.Fatal("Failed taking backup of YBA, aborting upgrade: " + errB.Error())
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

			if err := state.TransitionStatus(ybactlstate.UpgradingStatus); err != nil {
				log.Fatal("cannot upgrade, invalid status transition: " + err.Error())
			}

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

			for service := range serviceManager.Services() {
				log.Info("About to upgrade component " + service.Name())
				if err := service.Upgrade(); err != nil {
					if rollback {
						rollbackUpgrade(backupDir, state)
					}
					log.Fatal("Upgrade of " + service.Name() + " failed: " + err.Error())
				}
				log.Info("Completed upgrade of component " + service.Name())
			}

			// Permissions update to be safe
			if err := common.SetAllPermissions(); err != nil {
				log.Fatal("error updating permissions for data and software directories: " + err.Error())
			}

			for service := range serviceManager.Services() {
				log.Info("About to restart component " + service.Name())
				if err := service.Restart(); err != nil {
					if rollback {
						rollbackUpgrade(backupDir, state)
					}
					log.Fatal("Failed restarting " + service.Name() + " after upgrade: " + err.Error())
				}
				log.Info("Completed restart of component " + service.Name())
			}

			if err := common.WaitForYBAReady(ybactl.Version); err != nil {
				if rollback {
					rollbackUpgrade(backupDir, state)
				}
				log.Fatal(fmt.Sprintf("Error waiting for YBA to respond ready: %s", err.Error()))
			}

			var statuses []common.Status
			//serviceOrder = append([]string{newDbServiceName}, serviceOrder...)
			for service := range serviceManager.Services() {
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
			common.PrintStatus(state.CurrentStatus.String(), statuses...)
			// Here ends the postgres minor version/no upgrade workflow

			// Upgrade yba-ctl. Maybe make separate function for this?
			if err := ybaCtl.Install(); err != nil {
				log.Fatal("failed to upgrade yba-ctl")
			}

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
