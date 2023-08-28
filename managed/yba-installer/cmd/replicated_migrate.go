package cmd

import (
	"fmt"
	"os"
	"regexp"
	"sort"
	"time"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replicatedctl"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var baseReplicatedMigration = &cobra.Command{
	Use:   "replicated-migrate",
	Short: "Commands to handle migrating from replicated to a YBA-Installer instance.",
}

var replicatedMigrationStart = &cobra.Command{
	Use:   "start",
	Short: "start the replicated migration process.",
	Long: "Start the process to migrat from replicated to YBA-Installer. This will migrate all data" +
		" and configs from the replicated YugabyteDB Anywhere install to one managed by YBA-Installer." +
		" The migration will stop, but not delete, all replicated app instances.",
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

		// Mark install state
		state.CurrentStatus = ybactlstate.MigratingStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("before replicated migration, failed to update state: " + err.Error())
		}

		// Dump replicated settings
		replCtl := replicatedctl.New(replicatedctl.Config{})
		config, err := replCtl.AppConfigExport()
		if err != nil {
			log.Fatal("failed to export replicated app config: " + err.Error())
		}

		// Migrate config
		err = config.ExportYbaCtl()
		if err != nil {
			log.Fatal("failed to migrated replicated config to yba-ctl config: " + err.Error())
		}

		//re-init after exporting config
		initServices()

		// Lay out new YBA bits, don't start
		common.Install(common.GetVersion())
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
		common.MkdirAllOrFail(replBackupDir, os.ModePerm)
		CreateReplicatedBackupScript(replBackupDir, config.Get("storage_path").Value,
			config.Get("dbuser").Value, config.Get("db_external_port").Value, true, plat)

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
			pg.finishMigrateFromReplicated()
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

		common.WaitForYBAReady()

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

		// Update install state
		state.CurrentStatus = ybactlstate.InstalledStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("after full install, failed to update state: " + err.Error())
		}
	},
}

var replicatedMigrationConfig = &cobra.Command{
	Use:	 	"config",
	Short:	"generated yba-ctl.yml equivalent of replicated config",
	Run: func(cmd *cobra.Command, args[]string) {
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

func init() {
	replicatedMigrationStart.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip by name")
	replicatedMigrationStart.Flags().StringVarP(&licensePath, "license-path", "l", "",
		"path to license file")

	baseReplicatedMigration.AddCommand(replicatedMigrationStart, replicatedMigrationConfig)
	rootCmd.AddCommand(baseReplicatedMigration)
}
