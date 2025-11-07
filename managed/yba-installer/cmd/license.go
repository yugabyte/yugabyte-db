/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cmd

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/licensing/license"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

var licensePath string

var baseLicenseCmd = &cobra.Command{
	Use:   "license",
	Short: "Licensing commands for yugabyte. Manage the active license or validate a new license.",
}

var validateLicenseCmd = &cobra.Command{
	Use:   "validate [-l file]",
	Short: "Validate yugabyte license.",
	Long:  "Validate either the active license file, or provide a license using '-l'",
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
			fmt.Println("No licensing found for YugabyteDB Anywhere.")
			os.Exit(1)
		}

		if !lic.Validate() {
			fmt.Println("Found an invalid license for YugabyteDB Anywhere")
			os.Exit(1)
		}
		fmt.Println("Found valid YugabyteDB Anywhere license.")
	},
}

var addLicenseCmd = &cobra.Command{
	Use:     "add -l license_file",
	Short:   "Add a license for YugabyteDB Anywhere. (alias: update)",
	Long:    "Add a license for YugabyteDB Anywhere. This can also overwrite an existing license.",
	Aliases: []string{"update"},
	Args:    cobra.NoArgs,
	Run: func(cmd *cobra.Command, args []string) {
		if _, err := os.Stat(licensePath); err != nil {
			log.Fatal("Invalid license path given. Please provide a valid license with --license-path")
		}
		InstallLicense()
		log.Info("Added license, services can be started now")
	},
}

var printLicenseCmd = &cobra.Command{
	Use:   "print",
	Short: "Print the license for YugabyteDB Anywhere.",
	Args:  cobra.NoArgs,
	Run: func(cmd *cobra.Command, args []string) {
		var lic *license.License
		var err error
		if licensePath == "" {
			lic, err = license.FromInstalledLicense()
			if err != nil {
				log.Fatal("failed to get installed license: " + err.Error())
			}
		} else {
			lic, err = license.FromFile(licensePath)
			if err != nil {
				log.Fatal("failed to get license from file: " + err.Error())
			}
		}
		decoded, err := base64.StdEncoding.DecodeString(lic.EncodedData)
		if err != nil {
			log.Fatal("failed to decode license: " + err.Error())
		}
		var jsonData interface{}
		if err := json.Unmarshal(decoded, &jsonData); err != nil {
			log.Fatal("failed to parse license JSON: " + err.Error())
		}
		prettyJSON, err := json.MarshalIndent(jsonData, "", "  ")
		if err != nil {
			log.Fatal("failed to format license JSON: " + err.Error())
		}
		fmt.Println(string(prettyJSON))
	},
}

// License prints out any licensing requirements that YBA Installer currently has
// (none currently).
func InstallLicense() {
	lic, err := license.FromFile(licensePath)
	if err != nil {
		log.Fatal("invalid license file given: " + err.Error())
	}
	if !lic.Validate() {
		log.Fatal("invalid license")
	}
	if err := lic.WriteToLocation(common.LicenseFile()); err != nil {
		log.Fatal("failed to install license: " + err.Error())
	}
}

func init() {
	baseLicenseCmd.AddCommand(addLicenseCmd)
	baseLicenseCmd.AddCommand(validateLicenseCmd)
	baseLicenseCmd.AddCommand(printLicenseCmd)
	baseLicenseCmd.PersistentFlags().StringVarP(&licensePath, "license-path", "l", "",
		"Path to a YugabyteDB Anywhere license file")
	addLicenseCmd.MarkFlagRequired("license-path")

	rootCmd.AddCommand(baseLicenseCmd)
}
