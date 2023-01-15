/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/licensing/license"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/licensing/pubkey"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

var licensePath string

var baseLicenseCmd = &cobra.Command{
	Use:   "license",
	Short: "Licensing commands for yugabyte",
}

var validateLicenseCmd = &cobra.Command{
	Use:   "validate",
	Short: "Validate yugabyte license.",
	Args:  cobra.NoArgs,
	Run: func(cmd *cobra.Command, args []string) {
		var lic *license.License
		var err error
		if licensePath == "" {
			lic, err = license.FromInstalledLicense()
		} else {
			lic, err = license.FromFile(licensePath)
		}
		if err != nil {
			fmt.Println("No licensing found for Yugabyte Anywhere.")
			os.Exit(1)
		}

		if !pubkey.Validate(lic.Sha256Data(), lic.Signature) {
			fmt.Println("Found an invalid license for Yugabyte Anywhere")
			os.Exit(1)
		}
		fmt.Println("Found valid Yugabyte Anywhere license.")
	},
}

var updateLicenseCmd = &cobra.Command{
	Use:   "update",
	Short: "Add license to yba install",
	Args:  cobra.NoArgs,
	Run: func(cmd *cobra.Command, args []string) {
		InstallLicense()
		log.Info("Updated license, services can be started now")
	},
}

// License prints out any licensing requirements that YBA Installer currently has
// (none currently).
func InstallLicense() {
	lic, err := license.FromFile(licensePath)
	if err != nil {
		log.Fatal("invalid license file given: " + err.Error())
	}
	if !pubkey.Validate(lic.Sha256Data(), lic.Signature) {
		log.Fatal("invalid license")
	}
	if err := lic.WriteToLocation(common.LicenseFileInstall); err != nil {
		log.Fatal("failed to install license: " + err.Error())
	}
}

func init() {
	updateLicenseCmd.Flags().StringVarP(&licensePath, "license-path", "l", "", "path to license file")
	updateLicenseCmd.MarkFlagRequired("license-path")
	baseLicenseCmd.AddCommand(updateLicenseCmd)
	baseLicenseCmd.Flags().StringVarP(&licensePath, "license-path", "l", "",
		"validate given license instead of installed license")
	rootCmd.AddCommand(baseLicenseCmd)
}
