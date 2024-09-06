/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateGCPEARCmd represents the ear command
var updateGCPEARCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere GCP encryption at rest (EAR) configuration",
	Long:    "Update a GCP encryption at rest (EAR) configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		configNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(configNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No encryption at rest config name found to update\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		config, err := earutil.GetEARConfig(authAPI, configName, util.GCPEARType)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		gcp := config.GCP
		if gcp == nil {
			logrus.Fatalf("No GCP details found for %s\n", configName)
		}

		requestBody := make(map[string]interface{})

		hasUpdates := false

		gcsFilePath, err := cmd.Flags().GetString("credentials-file-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(gcsFilePath) == 0 {
			gcsCreds, err := util.GcpGetCredentialsAsMap()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debug("Updating GCP credentials\n")
			hasUpdates = true
			requestBody[util.GCPConfigField] = gcsCreds
		}

		if hasUpdates {
			earutil.UpdateEARConfig(
				authAPI,
				configName,
				config.ConfigUUID,
				util.GCPEARType,
				requestBody)
			return
		}
		logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))

	},
}

func init() {
	updateGCPEARCmd.Flags().SortFlags = false

	updateGCPEARCmd.Flags().String("credentials-file-path", "",
		"[Optional] Update GCP Credentials File Path.")

}
