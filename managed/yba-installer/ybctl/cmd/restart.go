package cmd

import "github.com/spf13/cobra"

var restartCmd = &cobra.Command{
	Use: "restart [serviceName]",
	Short: "The restart command is used to restart service(s) required for your Yugabyte " +
		"Anywhere installation.",
	Long: `
    The restart command can be invoked to stop any service that is required for the
    running of Yugabyte Anywhere. Can be invoked without any arguments to restart all
    services, or invoked with a specific service name to restart only that service.
    Valid service names: postgres, prometheus, yb-platform`,
}

func init() {
	rootCmd.AddCommand(restartCmd)
}
