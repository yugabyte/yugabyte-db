package cmd

import (
	"log"

	"github.com/spf13/cobra"
)

var rootCmd = &cobra.Command{
	Use:   "ybctl",
	Short: "YBA Installer is used to install Yugabyte Anywhere in an automated manner.",
	Long: `
    YBA Installer is your one stop shop for deploying Yugabyte Anywhere! Through
    YBA Installer, you can perform numerous actions related to your Yugabyte
    Anywhere instance through our command line CLI, such as clean, createBackup,
    restoreBackup, install, and upgrade! View the CLI menu to learn more!`,
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		log.Fatal(err)
	}
}
