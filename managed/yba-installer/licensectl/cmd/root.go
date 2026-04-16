package cmd

import (
	"github.com/spf13/cobra"
)

var rootCmd = &cobra.Command{
	Use:   "licensectl",
	Short: "creating and viewing a license",
}

// Execute will run the cli
func Execute() {
	if err := rootCmd.Execute(); err != nil {
		panic(err)
	}
}
