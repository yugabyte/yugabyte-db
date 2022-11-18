package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
)

var versionCmd = &cobra.Command{
	Use:   "version",
	Short: "The version of YBA Installer.",
	Args:  cobra.NoArgs,
	Long: `
    The version will be the same as the version of Yugabyte Anywhere that you will be
	installing when you invove the yba-ctl binary using the install command line option.`,
	Run: func(cmd *cobra.Command, args []string) {
		version := "1.0"
		fmt.Println(version)
		os.Exit(0)
	},
}

func init() {
	rootCmd.AddCommand(versionCmd)
}
