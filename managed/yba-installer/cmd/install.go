package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight/checks"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var dataless bool

var installCmd = &cobra.Command{
	Use:   "install",
	Short: "Install YugabyteDB Anywhere.",
	Long: `
        The install command will install the version of YugabyteDB Anywhere associated with the
				downloaded version of YBA Installer onto the local machine.
        `,
	Args: cobra.NoArgs,
	PreRun: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			if _, err := os.Stat(common.YbaInstalledMarker()); err != nil {
				log.Fatal("YugabyteDB Anywhere already installed, cannot install twice.")
			}
		} else if state.CurrentStatus == ybactlstate.InstalledStatus {
			log.Fatal("YugabyteDB Anywhere already installed, cannot install twice.")
		}
		if common.RunFromInstalled() {
			log.Fatal("install must be run from the yba bundle that is getting installed.")
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("failed to initialize state " + err.Error())
		}
		if len(viper.GetString("server_cert_path")) == 0 {
			log.Debug("marking self signed cert in ybactlstate")
			state.Config.SelfSignedCert = true
		}
		// Save the services installed
		state.Services.PerfAdvisor = viper.GetBool("perfAdvisor.enabled")
		state.Services.Platform = true
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
		// TODO: Add preflight checks for ybdb.
		if dataless {
			skippedPreflightChecks = append(skippedPreflightChecks, "disk-availability")
		}
		var results *checks.MappedResults

		if common.IsPerfAdvisorEnabled() && common.IsPostgresEnabled() {
			// Run both Perf Advisor and Postgres checks, then merge results
			paResults := preflight.Run(preflight.InstallPerfAdvisorChecks, skippedPreflightChecks...)
			pgResults := preflight.Run(preflight.InstallChecksWithPostgres, skippedPreflightChecks...)
			results = checks.MergeMappedResults(paResults, pgResults)

			combined := append(preflight.InstallPerfAdvisorChecks, preflight.InstallChecksWithPostgres...)
			deduped := deduplicateChecks(combined)
			results = preflight.Run(deduped, skippedPreflightChecks...)
		} else if common.IsPerfAdvisorEnabled() {
			results = preflight.Run(preflight.InstallPerfAdvisorChecks, skippedPreflightChecks...)
		} else if common.IsPostgresEnabled() {
			results = preflight.Run(preflight.InstallChecksWithPostgres, skippedPreflightChecks...)
		} else {
			results = preflight.Run(preflight.InstallChecks, skippedPreflightChecks...)
		}
		// Only print results if we should fail.
		if preflight.ShouldFail(results) {
			preflight.PrintPreflightResults(results)
			log.Fatal("Preflight checks failed. To skip (not recommended), " +
				"rerun the command with --skip_preflight <check name1>,<check name2>")
		}

		if err := ybactlstate.ValidatePrometheusScrapeConfig(); err != nil {
			log.Fatal("failed to validate prometheus scrape config: " + err.Error())
		}

		// Mark install start.
		state.CurrentStatus = ybactlstate.InstallingStatus
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("failed to write state: " + err.Error())
		}

		if err := common.Install(ybaCtl.Version()); err != nil {
			log.Fatal(fmt.Sprintf("error installing new ybactl %s: %s", ybaCtl.Version(), err.Error()))
		}

		if !dataless {
			if err := common.Initialize(); err != nil {
				log.Fatal("error initializing common components: " + err.Error())
			}
		}

		for service := range serviceManager.Services() {
			log.Info("About to install component " + service.Name())
			if err := service.Install(); err != nil {
				log.Fatal("Failed while installing " + service.Name() + ": " + err.Error())
			}
			if !dataless {
				if err := service.Initialize(); err != nil {
					log.Fatal("Failed while initializing " + service.Name() + ": " + err.Error())
				}
			} else {
				log.Debug("skipping initializing of service" + service.Name())
			}
			log.Info("Completed installing component " + service.Name())
		}

		// Update permissions of data and software to service username
		if dataless {
			if err := common.SetSoftwarePermissions(); err != nil {
				log.Fatal("error updating permissions for software directory: " + err.Error())
			}
		} else {
			if err := common.SetAllPermissions(); err != nil {
				log.Fatal("error updating permissions for software and data directories: " + err.Error())
			}
		}

		// Update state config now that install is complete.
		state.Config.Hostname = viper.GetString("host")
		state.CurrentStatus = ybactlstate.InstalledStatus
		// We are initialized if a full install has taken place.
		state.Initialized = !dataless
		if err := ybactlstate.StoreState(state); err != nil {
			log.Fatal("after full install, failed to update state: " + err.Error())
		}

		if dataless {
			log.Info("Install without data complete. Please run \"ybactl start\" to start " +
				"YugabyteDB Anywhere.")
			return
		}
		if err := common.WaitForYBAReady(ybaCtl.Version()); err != nil {
			log.Fatal(err.Error())
		}

		getAndPrintStatus(state)
		log.Info("Successfully installed YugabyteDB Anywhere!")
	},
}

func getAndPrintStatus(state *ybactlstate.State) {
	var statuses []common.Status
	for service := range serviceManager.Services() {
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

	common.PrintStatus(state.CurrentStatus.String(), statuses...)
}

// Deduplicate checks by Name
func deduplicateChecks(checks []preflight.Check) []preflight.Check {
	seen := make(map[string]bool)
	unique := make([]preflight.Check, 0, len(checks))

	for _, check := range checks {
		if !seen[check.Name()] {
			seen[check.Name()] = true
			unique = append(unique, check)
		}
	}
	return unique
}

func init() {
	installCmd.Flags().StringSliceVarP(&skippedPreflightChecks, "skip_preflight", "s",
		[]string{}, "Preflight checks to skip by name")
	installCmd.Flags().StringVarP(&licensePath, "license-path", "l", "", "path to license file")
	installCmd.Flags().BoolVar(&dataless, "without-data", false,
		"Install without initializing the data directory or starting services")

	// Install must be run from directory of yba version
	rootCmd.AddCommand(installCmd)
}
