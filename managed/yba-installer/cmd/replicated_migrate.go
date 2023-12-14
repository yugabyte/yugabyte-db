package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"syscall"
	"time"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replflow"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replicatedctl"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var baseReplicatedMigration = &cobra.Command{
	Use:   "replicated-migrate",
	Short: "Commands to handle migrating from replicated to a YBA-Installer instance. [EA]",
	Long: `replicated-migrate subcommands provide a workflow to move from a replicated based install
to one managed by YBA Installer. The general workflow should follow:
1. [Optional] config: Generate the yba-ctl.yml from replicated config
2. start: Start the migration. If the config has not been created, it will happen now.
3. finish: Complete the migration

Rollback is also available to move back to a replicated install if and only if 'finish' has not been
run and you have not upgraded during migration.

NOTE: THIS FEATURE IS EARLY ACCESS
`,
}

var replicatedMigrationStart = &cobra.Command{
	Use:   "start",
	Short: "start the replicated migration process. [EA]",
	Long: `Start the process to migrate from replicated to YBA-Installer. This will migrate all data
and configs from the replicated YugabyteDB Anywhere install to one managed by YBA-Installer.
The migration will stop, but not delete, all replicated app instances.

NOTE: THIS FEATURE IS EARLY ACCESS
` ,
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("failed to initialize state " + err.Error())
		}

		if err := ybaCtl.Install(); err != nil {
			log.Fatal("failed to install yba-ctl: " + err.Error())
		}

		// Install the license if it is provided.
		if licensePath != "" {
			InstallLicense()
		}

		// Run Preflight checks
		results := preflight.Run(preflight.ReplicatedMigrateChecks, skippedPreflightChecks...)
		if preflight.ShouldFail(results) {
			preflight.PrintPreflightResults(results)
			log.Fatal("Preflight checks failed. To skip (not recommended), " +
				"rerun the command with --skip_preflight <check name1>,<check name2>")
		}

		if err := state.TransitionStatus(ybactlstate.MigratingStatus); err != nil {
			log.Fatal("failed to update state: " + err.Error())
		}

		// Dump replicated settings
		replCtl := replicatedctl.New(replicatedctl.Config{})
		config, err := replCtl.AppConfigExport()
		if err != nil {
			log.Fatal("failed to export replicated app config: " + err.Error())
		}

		configView, err := replCtl.AppConfigView()
		if err != nil {
			log.Fatal("failed to get the config view: " + err.Error())
		}

		// Get the uid and gid used by the prometheus container. This is used for rollback.
		entry := config.Get("storage_path")
		var replicatedInstallRoot string
		if entry == replicatedctl.NilConfigEntry {
			replicatedInstallRoot = "/opt/yugabyte"
		} else {
			replicatedInstallRoot = entry.Value
		}
		common.SetReplicatedBaseDir(replicatedInstallRoot)
		state.Replicated.StoragePath = replicatedInstallRoot
		checkFile := filepath.Join(replicatedInstallRoot, "prometheusv2/queries.active")
		info, err := os.Stat(checkFile)
		if err != nil {
			log.Fatal("Could not read " + checkFile + " to get group and user: " + err.Error())
		}
		statInfo := info.Sys().(*syscall.Stat_t)
		log.DebugLF(
			fmt.Sprintf("Prometheus user:group ownership - '%s:%s'", statInfo.Uid, statInfo.Gid))
		state.Replicated.PrometheusFileUser = statInfo.Uid
		state.Replicated.PrometheusFileGroup = statInfo.Gid

		version, err := replflow.YbaVersion(config)
		if err != nil {
			log.Fatal("unable to validate running YBA version: " + err.Error())
		}
		if version != ybaCtl.Version() {
			prompt := "Detected version mismatch between active YBA and migration target version. " +
				"Rollback will not be allowed once YBA is running after migration start. Continue?"
			if !common.UserConfirm(prompt, common.DefaultNo) {
				log.Fatal("not starting migration")
			}
		}
		state.Replicated.OriginalVersion = version

		// Mark install state. Do this manually, as we are also updating additional fields.
		state.CurrentStatus = ybactlstate.MigratingStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("before replicated migration, failed to update state: " + err.Error())
		}

		// Migrate config
		err = config.ExportYbaCtl()
		if err != nil {
			log.Fatal("failed to migrated replicated config to yba-ctl config: " + err.Error())
		}

		//re-init after exporting config
		initServices()

		// Lay out new YBA bits, don't start
		common.Install(ybaCtl.Version())
		for _, name := range serviceOrder {
			log.Info("About to migrate component " + name)
			if err := services[name].MigrateFromReplicated(); err != nil {
				log.Fatal("Failed while migrating " + name + ": " + err.Error())
			}
			log.Info("Completed migrating component " + name)
		}

		// Cast the plat struct because we will use it throughout migration.
		plat, ok := services[YbPlatformServiceName].(Platform)
		if !ok {
			log.Fatal("Could not cast service to yb-platform.")
		}

		// Take a backup of running YBA using replicated settings.
		replBackupDir := "/tmp/replBackupDir"
		common.MkdirAllOrFail(replBackupDir, common.DirMode)
		dataDir, err := configView.Get("storage_path")
		if err != nil {
			log.Fatal("no storage path found in config view: " + err.Error())
		}
		dbUser, err := configView.Get("dbuser")
		if err != nil {
			log.Fatal("no dbuser found in config view: " + err.Error())
		}
		dbPort, err := configView.Get("db_external_port")
		if err != nil {
			log.Fatal("no db_external_port found in config view: " + err.Error())
		}
		CreateReplicatedBackupScript(replBackupDir, dataDir.Get(), dbUser.Get(), dbPort.Get(),
			true, plat)

		// Stop replicated containers.
		log.Info("Waiting for Replicated to stop.")
		// Wait 10 minutes for Replicated app to return stopped status.
		success := false
		retriesCount := 20 // retry interval 30 seconds.
		for i := 0; i < retriesCount; i++ {
			if err := replCtl.AppStop(); err != nil {
				log.Warn("could not stop replicated app: " + err.Error())
			}
			if status, err := replCtl.AppStatus(); err == nil {
				result := status[0]
				if result.State == "stopped" {
					log.Info("Replicated app stopped.")
					success = true
					break
				}
			} else {
				log.Warn(fmt.Sprintf("Error getting app status: %s", err.Error()))
			}
			time.Sleep(30 * time.Second)
		}
		if success {
			log.Info("Replicated containers stopped. Starting up service based YBA.")
		} else {
			prompt := "Could not confirm Replicated containers stopped. Proceed with migration?"
			if !common.UserConfirm(prompt, common.DefaultNo) {
				log.Fatal("Stopping Replicated migration.")
			}
		}

		// Start postgres and prometheus processes.
		for _, name := range serviceOrder {
			// Skip starting YBA until we restore the new data to avoid migration conflicts.
			if name == YbPlatformServiceName {
				continue
			}
			if err := services[name].Start(); err != nil {
				log.Fatal("Failed while starting " + name + ": " + err.Error())
			}
			log.Info("Completed starting component " + name)
		}

		// Create yugaware postgres DB.
		if pg, ok := services[PostgresServiceName].(Postgres); ok {
			pg.replicatedMigrateStep2()
		}

		// Restore data using yugabundle method, pass in ybai data dir so that data can be copied over
		log.Info("Restoring data to newly installed YBA.")
		files, err := os.ReadDir(replBackupDir)
		if err != nil {
			log.Fatal(fmt.Sprintf("error reading directory %s: %s", replBackupDir, err.Error()))
		}
		// Looks for most recent backup first.
		sort.Slice(files, func(i, j int) bool {
			iinfo, e1 := files[i].Info()
			jinfo, e2 := files[j].Info()
			if e1 != nil || e2 != nil {
				log.Fatal("Error determining modification time for backups.")
			}
			return iinfo.ModTime().After(jinfo.ModTime())
		})
		// Find the old backup.
		for _, file := range files {
			log.Info(file.Name())
			match, _ := regexp.MatchString(`^backup_\d{2}-\d{2}-\d{2}-\d{2}-\d{2}\.tgz$`, file.Name())
			if match {
				input := fmt.Sprintf("%s/%s", replBackupDir, file.Name())
				log.Info(fmt.Sprintf("Restoring replicated backup %s to YBA.", input))
				RestoreBackupScript(input, common.GetBaseInstall(), false, true, plat, true, false)
				break
			}
		}

		// Start YBA once postgres is fully ready.
		if err := plat.Start(); err != nil {
			log.Fatal("Failed while starting yb-platform: " + err.Error())
		}
		log.Info("Started yb-platform after restoring data.")

		common.WaitForYBAReady(ybaCtl.Version())

		var statuses []common.Status
		for _, name := range serviceOrder {
			status, err := services[name].Status()
			if err != nil {
				log.Fatal("failed to get status: " + err.Error())
			}
			statuses = append(statuses, status)
			if !common.IsHappyStatus(status) {
				log.Fatal(status.Service + " is not running! Install might have failed, please check " +
					common.YbactlLogFile())
			}
		}

		common.PrintStatus(statuses...)
		log.Info("Successfully installed YugabyteDB Anywhere!")

		// Update state
		state.CurrentStatus = ybactlstate.MigrateStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("after full install, failed to update state: " + err.Error())
		}
	},
}

var replicatedMigrateFinish = &cobra.Command{
	Use:   "finish",
	Short: "Complete the replicated migration, fully deleting the replicated install [EA]",
	Long: "Complete the replicated migration. This will fully move data over from replicated to " +
		"yba installer, delete replicated data, and remove all replicated containers." +
		"\n\nNOTE: THIS FEATURE IS EARLY ACCESS",
	PreRun: func(cmd *cobra.Command, args []string) {
		prompt := `replicated-migrate finish will completely uninstall replicated, completing the
migration to yba-installer. This involves deleting the storage directory (default /opt/yugabyte).
Are you sure you want to continue?`
		if !common.UserConfirm(prompt, common.DefaultYes) {
			log.Info("Canceling finish")
			os.Exit(0)
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("failed to YBA Installer state: " + err.Error())
		}
		if err := state.TransitionStatus(ybactlstate.FinishingStatus); err != nil {
			log.Fatal("Failed to update status: " + err.Error())
		}

		for _, name := range serviceOrder {
			if err := services[name].FinishReplicatedMigrate(); err != nil {
				log.Fatal("could not finish replicated migration for " + name + ": " + err.Error())
			}
		}
		if err := replflow.Uninstall(); err != nil {
			log.Fatal("unable to uninstall replicated: " + err.Error())
		}
		state.CurrentStatus = ybactlstate.InstalledStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("Failed to save state: " + err.Error())
		}
	},
}

var replicatedMigrationConfig = &cobra.Command{
	Use:   "config",
	Short: "generated yba-ctl.yml equivalent of replicated config [EA]",
	Run: func(cmd *cobra.Command, args []string) {
		ctl := replicatedctl.New(replicatedctl.Config{})
		config, err := ctl.AppConfigExport()
		if err != nil {
			log.Fatal("failed to export replicated app config: " + err.Error())
		}
		err = config.ExportYbaCtl()
		if err != nil {
			log.Fatal("failed to migrated replicated config to yba-ctl: " + err.Error())
		}
	},
}

/*
 * Rollback will perform the following steps:
 * 1. Stop services started by yba-ctl
 * 2. Reset prometheus directory ownership
 * 3. Restart replicated app
 * 4. Remove any yba-installer yugaware install
 */
var replicatedRollbackCmd = &cobra.Command{
	Use: "rollback",
	Short: "allows rollback from an unfinished migrate back to replicated install. Any changes " +
		"made since migrate will not be available after rollback. [EA]",
	Long: "After a replicated migrate has been started and before the finish command runs, allows " +
		"rolling back to the replicated install. As this is a rollback, any changes made to YBA after " +
		"migrate will not be reflected after the rollback completes.\n\n" +
		"NOTE: THIS FEATURE IS EARLY ACCESS",
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("failed to YBA Installer state: " + err.Error())
		}

		if common.Version != state.Replicated.OriginalVersion &&
			state.CurrentStatus == ybactlstate.MigrateStatus {
			log.Debug("version change detected")
			if os.Getenv("YBA_MODE") == "dev" {
				prompt := "rollback is discouraged for version change after YBA is running. Continue?"
				if !common.UserConfirm(prompt, common.DefaultNo) {
					log.Fatal("Cannot rollback after migration has successfully started and there is a " +
						"YBA Version change")
				}
			} else {
				log.Fatal("Cannot rollback after migration has successfully started and there is a " +
					"YBA Version change")
			}
		}
		prompt := "Rollback to Replicated will not carry over any changes made to YBA after " +
			"migration began. Continue?"
		if !common.UserConfirm(prompt, common.DefaultNo) {
			log.Fatal("canceling rollback")
		}
		// Only transition state after we start the rollback
		if err := state.TransitionStatus(ybactlstate.RollbackStatus); err != nil {
			log.Fatal("failed to update statue: " + err.Error())
		}

		if err := rollbackMigrations(state); err != nil {
			log.Fatal("rollback failed: " + err.Error())
		}
	},
}

func rollbackMigrations(state *ybactlstate.State) error {
	log.Info("Stopping services")
	for _, name := range serviceOrder {
		if err := services[name].Stop(); err != nil {
			log.Fatal("Could not stop " + name + ": " + err.Error())
		}
	}

	// Update Prometheus directory ownership here
	prom := services[PrometheusServiceName].(Prometheus)
	err := prom.RollbackMigration(
		state.Replicated.PrometheusFileUser,
		state.Replicated.PrometheusFileUser,
		state.Replicated.StoragePath)
	if err != nil {
		log.Fatal("Failed to rollback prometheus migration: " + err.Error())
	}

	log.Info("Starting yugaware in replicated")
	replClient := replicatedctl.New(replicatedctl.Config{})
	if err := replClient.AppStart(); err != nil {
		return fmt.Errorf("failed to start yugaware: %w", err)
	}

	// 20 retries at 30 seconds each
	var success = false
	for i := 0; i < 20; i++ {
		if status, err := replClient.AppStatus(); err == nil {
			result := status[0]
			if result.State == "started" {
				log.Info("Replicated app started.")
				success = true
				break
			}
		} else {
			log.Warn(fmt.Sprintf("Error getting app status: %s", err.Error()))
		}
		log.Info("replicated app not yet started, waiting...")
		time.Sleep(30 * time.Second)
	}
	if !success {
		return fmt.Errorf("Failed to restart yugaware in replicated")
	}

	log.Info("Removing yba-installer yugaware install")
	common.Uninstall(serviceOrder, true)
	return nil
}

func init() {
	replicatedMigrationStart.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip by name")
	replicatedMigrationStart.Flags().StringVarP(&licensePath, "license-path", "l", "",
		"path to license file")

	baseReplicatedMigration.AddCommand(replicatedMigrationStart, replicatedMigrationConfig,
		replicatedMigrateFinish, replicatedRollbackCmd)

	// Feature flag replicated migration for now
	rootCmd.AddCommand(baseReplicatedMigration)
}
