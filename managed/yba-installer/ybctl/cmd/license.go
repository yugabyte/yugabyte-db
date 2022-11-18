package cmd

import "github.com/spf13/cobra"

var licenseCmd = &cobra.Command{
	Use:   "license",
	Short: "The license command prints out YBA Installer licensing requirements.",
	Long: `
    The license command prints out any licensing requirements associated with
    YBA Installer in order for customers to run it. Currently there are no licensing
    requirements for YBA Installer, but that could change in the future.
    `,
}

func init() {
	rootCmd.AddCommand(licenseCmd)
}
