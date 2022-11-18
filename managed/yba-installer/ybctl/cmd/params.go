package cmd

import "github.com/spf13/cobra"

var paramsCmd = &cobra.Command{
	Use:   "params key value",
	Short: "The params command can be used to update entries in the user configuration file.",
	Long: `
    The params command is used to update configuration entries in yba-installer-input.yml,
    corresponding to the settings for your Yugabyte Anywhere installation. Note that invoking
    this command will update your configuration files, but will not restart any services for you.
    Use the reconfigure command for that alternative.`,
}

func init() {
	rootCmd.AddCommand(paramsCmd)
}
