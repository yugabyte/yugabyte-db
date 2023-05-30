package cmd

import (
	"log"
	"path/filepath"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
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
