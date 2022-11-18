package cmd

import "github.com/spf13/cobra"

var preflightCmd = &cobra.Command{
	Use: "preflight [list]",
	Short: "The preflight command checks makes sure that your system is ready to " +
		"install Yugabyte Anywhere.",
	Long: `
		The preflight command goes through a series of Preflight checks that each have a
		critcal and warning level, and alerts you if these requirements are not met on your
		Operating System.`,
}

func init() {
	rootCmd.AddCommand(preflightCmd)
}
