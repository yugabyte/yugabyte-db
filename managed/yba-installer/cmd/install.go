package cmd

import (
	"os"

	"github.com/spf13/cobra"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var installCmd = &cobra.Command{
	Use:   "install",
	Short: "Install YugabyteDB Anywhere.",
	Long: `
        The install command will install the version of YugabyteDB Anywhere associated with the
				downloaded version of YBA Installer onto the local machine.
        `,
	Args: cobra.NoArgs,
	PreRun: func(cmd *cobra.Command, args []string) {
		if _, err := os.Stat(common.YbaInstalledMarker()); err == nil {
			log.Fatal("YugabyteDB Anywhere already installed, cannot install twice.")
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("failed to initialize state " + err.Error())
		}
		if err := state.TransitionStatus(ybactlstate.InstallingStatus); err != nil {
			log.Fatal("failed to start install: " + err.Error())
		}

		if err := ybaCtl.Install(); err != nil {
			log.Fatal("failed to install yba-ctl: " + err.Error())
		}

		// Install the license if it is provided.
		if licensePath != "" {
			InstallLicense()
		}

		// Preflight checks
		results := preflight.Run(preflight.InstallChecksWithPostgres, skippedPreflightChecks...)
		// Only print results if we should fail.
		if preflight.ShouldFail(results) {
			preflight.PrintPreflightResults(results)
			log.Fatal("Preflight checks failed. To skip (not recommended), " +
				"rerun the command with --skip_preflight <check name1>,<check name2>")
		}

		// Mark install start.
		state.CurrentStatus = ybactlstate.InstallingStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("failed to write state: " + err.Error())
		}

		common.Install(ybaCtl.Version())

		for _, name := range serviceOrder {
			log.Info("About to install component " + name)
			if err := services[name].Install(); err != nil {
				log.Fatal("Failed while installing " + name + ": " + err.Error())
			}
			log.Info("Completed installing component " + name)
		}
		state.CurrentStatus = ybactlstate.InstalledStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("after full install, failed to update state: " + err.Error())
		}
		common.WaitForYBAReady(ybaCtl.Version())

		var statuses []common.Status
		for _, service := range services {
			status, err := service.Status()
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
	installCmd.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip by name")
	installCmd.Flags().StringVarP(&licensePath, "license-path", "l", "", "path to license file")

	// Install must be run from directory of yba version
	rootCmd.AddCommand(installCmd)
}
