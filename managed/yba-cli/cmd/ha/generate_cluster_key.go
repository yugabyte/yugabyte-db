/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ha

import (
	"encoding/json"
	"fmt"
	"io"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var generateClusterKeyCmd = &cobra.Command{
	Use:     "generate-cluster-key",
	Aliases: []string{"generate-key", "gen-key"},
	Short:   "Generate a new cluster key",
	Long:    "Generate a new cluster key for HA configuration (44 characters)",
	Example: `yba ha generate-cluster-key`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		response, err := authAPI.GenerateClusterKey().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA", "Generate Cluster Key")
		}
		defer response.Body.Close()

		body, err := io.ReadAll(response.Body)
		if err != nil {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("Error reading cluster key response: %s\n", err.Error()),
					formatter.RedColor,
				),
			)
		}

		var result struct {
			ClusterKey string `json:"cluster_key"`
		}
		if err := json.Unmarshal(body, &result); err != nil {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("Error parsing cluster key response: %s\n", err.Error()),
					formatter.RedColor,
				),
			)
		}

		if result.ClusterKey == "" {
			logrus.Fatalf(
				formatter.Colorize("Failed to generate cluster key\n", formatter.RedColor),
			)
		}

		logrus.Infof("Generated cluster key: %s\n",
			formatter.Colorize(result.ClusterKey, formatter.GreenColor))
	},
}
