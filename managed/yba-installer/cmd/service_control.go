package cmd

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
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
	Run: func(cmd *cobra.Command, args []string) {
		if len(args) == 1 {
			services[args[0]].Start()
		} else {
			for _, name := range serviceOrder {
				services[name].Start()
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
	Run: func(cmd *cobra.Command, args []string) {
		if len(args) == 1 {
			services[args[0]].Stop()
		} else {
			for _, name := range serviceOrder {
				services[name].Stop()
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
	Run: func(cmd *cobra.Command, args []string) {
		if len(args) == 1 {
			services[args[0]].Restart()
		} else {
			for _, name := range serviceOrder {
				services[name].Restart()
			}
		}
	},
}

func init() {
	// Service control commands only work from the installed path
	if common.RunFromInstalled() {
		rootCmd.AddCommand(startCmd, stopCmd, restartCmd)
	}
}
