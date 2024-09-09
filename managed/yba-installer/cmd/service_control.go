package cmd

import (
	"path/filepath"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight/checks"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var startCmd = &cobra.Command{
	Use: "start [serviceName]",
	Short: "The start command is used to start service(s) required for your Yugabyte " +
		"Anywhere installation.",
	Long: `
    The start command can be invoked to start any service that is required for the
    running of YugabyteDB Anywhere. Can be invoked without any arguments to start all
    services, or invoked with a specific service name to start only that service.
    Valid service names: postgres, prometheus, yb-platform`,
	Args:      cobra.MatchAll(cobra.MaximumNArgs(1), cobra.OnlyValidArgs),
	ValidArgs: serviceOrder,
	PreRun: func(cmd *cobra.Command, args []string) {
		if !common.RunFromInstalled() {
			path := filepath.Join(common.YbactlInstallDir(), "yba-ctl")
			log.Fatal("start must be run from " + path +
				". It may be in the systems $PATH for easy of use.")
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("unable to load yba installer state: " + err.Error())
		}
		if state.CurrentStatus != ybactlstate.InstalledStatus {
			log.Fatal("cannot start services - need installed state got " +
				state.CurrentStatus.String())
		}

		// Initialize if it has not already happened. Do this instead of normal start workflow
		if !state.Initialized {
			// Run preflight check for data directory size if we have to initialize.
			results := preflight.Run([]preflight.Check{checks.DiskAvail})
			preflight.PrintPreflightResults(results)
			if preflight.ShouldFail(results) {
				log.Fatal("preflight failed")
			}
			log.Info("Initializing YBA before starting services")
			if err := common.Initialize(); err != nil {
				log.Fatal("Failed to initialize common components: " + err.Error())
			}
			for _, name := range serviceOrder {
				if err := services[name].Initialize(); err != nil {
					log.Fatal("Failed to initialize " + name + ": " + err.Error())
				}
			}
			state.Initialized = true
			if err := ybactlstate.StoreState(state); err != nil {
				log.Fatal("failed to update state: " + err.Error())
			}
			if err := common.WaitForYBAReady(ybaCtl.Version()); err != nil {
				log.Fatal("failed to wait for yba ready: " + err.Error())
			}
			getAndPrintStatus()
			// We can exit early, as initialize will also start the services
			return
		}

		if err := common.CheckDataVersionFile(); err != nil {
			log.Fatal("Failed to validate data version: " + err.Error())
		}
		if len(args) == 1 {
			if err := services[args[0]].Start(); err != nil {
				log.Fatal("Failed to start " + args[0] + ": " + err.Error())
			}
		} else {
			for _, name := range serviceOrder {
				if err := services[name].Start(); err != nil {
					log.Fatal("Failed to start " + name + ": " + err.Error())
				}
			}
		}
	},
}

var stopCmd = &cobra.Command{
	Use: "stop [serviceName]",
	Short: "The stop command is used to stop service(s) required for your Yugabyte " +
		"Anywhere installation.",
	Long: `
    The stop command can be invoked to stop any service that is required for the
    running of YugabyteDB Anywhere. Can be invoked without any arguments to stop all
    services, or invoked with a specific service name to stop only that service.
    Valid service names: postgres, prometheus, yb-platform`,
	Args:      cobra.MatchAll(cobra.MaximumNArgs(1), cobra.OnlyValidArgs),
	ValidArgs: serviceOrder,
	PreRun: func(cmd *cobra.Command, args []string) {
		if !common.RunFromInstalled() {
			path := filepath.Join(common.YbactlInstallDir(), "yba-ctl")
			log.Fatal("stop must be run from " + path +
				". It may be in the systems $PATH for easy of use.")
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("unable to load yba installer state: " + err.Error())
		}
		if state.CurrentStatus != ybactlstate.InstalledStatus {
			log.Fatal("cannot stop services - need installed state got " +
				state.CurrentStatus.String())
		}
		if len(args) == 1 {
			if err := services[args[0]].Stop(); err != nil {
				log.Fatal("Failed to stop " + args[0] + ": " + err.Error())
			}
		} else {
			for _, name := range serviceOrder {
				if err := services[name].Stop(); err != nil {
					log.Fatal("Failed to stop " + name + ": " + err.Error())
				}
			}
		}
	},
}

var restartCmd = &cobra.Command{
	Use: "restart [serviceName]",
	Short: "The restart command is used to restart service(s) required for your Yugabyte " +
		"Anywhere installation.",
	Long: `
    The restart command can be invoked to stop any service that is required for the
    running of YugabyteDB Anywhere. Can be invoked without any arguments to restart all
    services, or invoked with a specific service name to restart only that service.
    Valid service names: postgres, prometheus, yb-platform`,
	Args:      cobra.MatchAll(cobra.MaximumNArgs(1), cobra.OnlyValidArgs),
	ValidArgs: serviceOrder,
	PreRun: func(cmd *cobra.Command, args []string) {
		if !common.RunFromInstalled() {
			path := filepath.Join(common.YbactlInstallDir(), "yba-ctl")
			log.Fatal("restart must be run from " + path +
				". It may be in the systems $PATH for easy of use.")
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("unable to load yba installer state: " + err.Error())
		}
		if state.CurrentStatus != ybactlstate.InstalledStatus {
			log.Fatal("cannot restart services - need installed state got " +
				state.CurrentStatus.String())
		}
		if len(args) == 1 {
			if err := services[args[0]].Restart(); err != nil {
				log.Fatal("Failed to restart " + args[0] + ": " + err.Error())
			}
		} else {
			for _, name := range serviceOrder {
				if err := services[name].Restart(); err != nil {
					log.Fatal("Failed to restart " + name + ": " + err.Error())
				}
			}
		}
	},
}

func init() {
	rootCmd.AddCommand(startCmd, stopCmd, restartCmd)
}
