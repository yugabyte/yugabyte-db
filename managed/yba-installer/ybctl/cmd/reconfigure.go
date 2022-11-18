package cmd

import "github.com/spf13/cobra"

var reConfigureCmd = &cobra.Command{
	Use: "reconfigure [key] [value]",
	Short: "The reconfigure command updates config entries in yba-installer-input.yml " +
		"if desired, and restarts all Yugabyte Anywhere services.",
	Long: `
    The reconfigure command is used to update configuration entries in the user configuration file
    yba-installer-input.yml, and performs a restart of all Yugabyte Anywhere services to make the
    changes from the updated configuration take effect. It is possible to invoke this method in
    one of two ways. Executing reconfigure without any arguments will perform a simple restart
    of all Yugabyte Anywhere services without any updates to the configuration files. Executing
    reconfigure with a key and value argument pair (the configuration setting you want to update)
    will update the configuration files accordingly, and restart all Yugabyte Anywhere services.
    `,
}

func init() {
	rootCmd.AddCommand(reConfigureCmd)
}
