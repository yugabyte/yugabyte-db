//go:build dev

// Add the skip_version_checks flag for dev testing builds.

package cmd

func init() {
	rootCmd.PersistentFlags().BoolVar(&skipVersionChecks, "skip_version_checks", false,
		"Skip any checks for version mismatch. This can lead to failures.")
}
