package cmd

import "github.com/spf13/cobra"

var upgradeCmd = &cobra.Command{
	Use:   "upgrade",
	Short: "The upgrade command is used to upgrade an existing Yugabyte Anywhere installation.",
	Long: `
   The execution of the upgrade command will upgrade an already installed version of Yugabyte
   Anywhere present on your operating system, to the upgrade version associated with your download of
   YBA Installer. Please make sure that you have installed Yugabyte Anywhere using the install command
   prior to executing the upgrade command.
   `,
}

func init() {
	rootCmd.AddCommand(upgradeCmd)
}
