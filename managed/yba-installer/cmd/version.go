package cmd

import (
	"errors"
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components/yugaware"
)

var versionCmd = &cobra.Command{
	Use:   "version",
	Short: "Print the version of YBA Installer.",
	Args:  cobra.NoArgs,
	Long: `
    The version will be the same as the version of YugabyteDB Anywhere that you will be
	installing when you involve the yba-ctl binary using the install command line option.`,
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("YBA Installer: " + ybaCtl.Version())
		yugawareVersion, err := yugaware.InstalledVersionFromMetadata()
		if errors.Is(err, yugaware.NotInstalledVersionError) {
			yugawareVersion = err.Error()
		}
		fmt.Println("YugabyteDB Anywhere: " + yugawareVersion)
	},
}

func init() {
	rootCmd.AddCommand(versionCmd)
}
