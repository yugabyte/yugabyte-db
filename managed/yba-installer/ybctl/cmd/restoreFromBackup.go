package cmd

import "github.com/spf13/cobra"

var restoreBackup = &cobra.Command{
	Use:   "restoreBackup inputPath",
	Short: "The restoreBackup command restores a backup of your Yugabyte Anywhere instance.",
	Long: `
	The restoreBackup command executes our yb_platform_backup.sh that restores the backup of your
	Yugabyte Anywhere instance. Executing this command requires that you create and specify the
	inputPath where the backup .tar.gz file that will be restored is located as the first argument
	to restoreBackup.
	`,
}

func init() {
	rootCmd.AddCommand(restoreBackup)
}
