package cmd

import "github.com/spf13/cobra"

var cleanCmd = &cobra.Command{
	Use:   "clean",
	Short: "The clean command uninstalls your Yugabyte Anywhere instance.",
	Long: `
    The clean command performs a complete removal of your Yugabyte Anywhere
    Instance by stopping all services, removing data directories, and dropping the
    Yugabyte Anywhere database.`,
}

func init() {
	rootCmd.AddCommand(cleanCmd)
}
