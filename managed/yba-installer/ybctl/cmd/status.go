package cmd

import "github.com/spf13/cobra"

var statusCmd = &cobra.Command{
	Use: "status",
	Short: "The status command prints out the status of service(s) running as " +
		"part of your Yugabyte Anywhere installation.",
	Long: `
    The status command is used to print out the information corresponding to the
    status of all services related to Yugabyte Anywhere, or for just a particular service.
    For each service, the status command will print out the name of the service, the version of the
    service, the port the service is associated with, the location of any
    applicable systemd and config files, and the running status of the service
    (active or inactive)`,
}

func init() {
	rootCmd.AddCommand(statusCmd)
}
