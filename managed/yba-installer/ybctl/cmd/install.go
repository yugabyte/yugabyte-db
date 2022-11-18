package cmd

import "github.com/spf13/cobra"

var installCmd = &cobra.Command{
	Use:   "install",
	Short: "The install command is installs Yugabyte Anywhere onto your operating system.",
	Long: `
			The install command is the main workhorse command for YBA Installer that
			will install the version of Yugabyte Anywhere associated with your downloaded version
			of YBA Installer onto your host Operating System. Can also perform an install while skipping
			certain preflight checks if desired.
			`,
}

func init() {
	rootCmd.AddCommand(installCmd)
}
