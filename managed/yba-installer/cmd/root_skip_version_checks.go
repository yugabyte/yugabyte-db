package cmd

import "os"

func init() {
	// Add the skip_version_checks flag for YBA_MODE=dev
	if os.Getenv("YBA_MODE") == "dev" {
		rootCmd.PersistentFlags().BoolVar(&skipVersionChecks, "skip_version_checks", false,
			"Skip any checks for version mismatch. This can lead to failures.")
	}
}
