package cmd

import "github.com/spf13/cobra"

var stopCmd = &cobra.Command{
	Use: "stop [serviceName]",
	Short: "The stop command is used to stop service(s) required for your Yugabyte " +
		"Anywhere installation.",
	Long: `
    The stop command can be invoked to stop any service that is required for the
    running of Yugabyte Anywhere. Can be invoked without any arguments to stop all
    services, or invoked with a specific service name to stop only that service.
    Valid service names: postgres, prometheus, yb-platform`,
}

func init() {
	rootCmd.AddCommand(stopCmd)
}
