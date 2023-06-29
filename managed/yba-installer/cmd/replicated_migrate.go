package cmd

import (
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

		replCtl := replicatedctl.New(replicatedctl.Config{})
		if err := replCtl.AppStop(); err != nil {
			log.Fatal("could not stop replicated app: " + err.Error())
		}

		common.Install(common.GetVersion())

		for _, name := range serviceOrder {
			log.Info("About to migrate component " + name)
			if err := services[name].MigrateFromReplicated(); err != nil {
				log.Fatal("Failed while migrating " + name + ": " + err.Error())
			}
			log.Info("Completed migrating component " + name)
		}

		state.CurrentStatus = ybactlstate.InstalledStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("after full install, failed to update state: " + err.Error())
		}
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
	},
}

func init() {
	replicatedMigrationStart.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip by name")
	replicatedMigrationStart.Flags().StringVarP(&licensePath, "license-path", "l", "",
		"path to license file")

	baseReplicatedMigration.AddCommand(replicatedMigrationStart)
	rootCmd.AddCommand(baseReplicatedMigration)
}
