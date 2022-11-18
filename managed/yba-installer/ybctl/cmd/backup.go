package cmd

import "github.com/spf13/cobra"

var backupCmd = &cobra.Command{
	Use:   "createBackup outputPath",
	Short: "The createBackup command is used to take a backup of your Yugabyte Anywhere instance.",
	Long: `
	The createBackup command executes our yb_platform_backup.sh that creates a backup of your
	Yugabyte Anywhere instance. Executing this command requires that you create and specify the
	outputPath where you want the backup .tar.gz file to be stored as the first argument to
	createBackup.
	`,
}

func init() {
	rootCmd.AddCommand(backupCmd)
}
