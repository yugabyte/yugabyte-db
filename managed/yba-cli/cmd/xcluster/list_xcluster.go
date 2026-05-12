/*
 * Copyright (c) YugabyteDB, Inc.
 */

package xcluster

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/xcluster"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var listXClusterCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List a YugabyteDB Anywhere xcluster between two universes",
	Long:    "List a xcluster in YugabyteDB Anywhere between two universes",
	Example: `yba xcluster list --source-universe-name <source-universe-name> \
	  --target-universe-name <target-universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		sourceUniName, err := cmd.Flags().GetString("source-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		targetUniName, err := cmd.Flags().GetString("target-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(sourceUniName) ||
			util.IsEmptyString(targetUniName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Missing source or target universe name\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		sourceUniverseName, err := cmd.Flags().GetString("source-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		targetUniverseName, err := cmd.Flags().GetString("target-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sourceUniverse, targetUniverse := GetSourceAndTargetXClusterUniverse(
			authAPI, sourceUniverseName, targetUniverseName, "", "", "List")

		sourceDetails := sourceUniverse.GetUniverseDetails()
		sourceXClusterConfigList := sourceDetails.GetSourceXClusterConfigs()
		if len(sourceXClusterConfigList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"No source xcluster found in universe: %s\n",
						sourceUniverse.GetName(),
					),
					formatter.RedColor,
				))
		}

		targetDetails := targetUniverse.GetUniverseDetails()
		targetXClusterConfigList := targetDetails.GetTargetXClusterConfigs()
		if len(targetXClusterConfigList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"No target xcluster found in universe: %s\n",
						targetUniverse.GetName(),
					),
					formatter.RedColor,
				))
		}

		xclusterUUIDs := util.FindCommonStringElements(
			sourceXClusterConfigList,
			targetXClusterConfigList,
		)
		if len(xclusterUUIDs) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No xclusters found between universes %s and %s\n",
						sourceUniverse.GetName(), targetUniverse.GetName()),
					formatter.RedColor,
				))
		}

		r := make([]ybaclient.XClusterConfigGetResp, 0)
		for _, x := range xclusterUUIDs {
			rGet, response, err := authAPI.GetXClusterConfig(x).Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "xCluster", "List")
			}

			xclusterConfig := util.CheckAndDereference(
				rGet,
				fmt.Sprintf("No xcluster found with uuid %s", x),
			)

			r = append(r, xclusterConfig)
		}

		xcluster.SourceUniverse = sourceUniverse
		xcluster.TargetUniverse = targetUniverse

		xclusterCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  xcluster.NewXClusterFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No xclusters found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		xcluster.Write(xclusterCtx, r)

	},
}

func init() {
	listXClusterCmd.Flags().SortFlags = false

	listXClusterCmd.Flags().String("source-universe-name", "",
		"[Required] The name of the source universe for the xcluster.")
	listXClusterCmd.MarkFlagRequired("source-universe-name")
	listXClusterCmd.Flags().String("target-universe-name", "",
		"[Required] The name of the target universe for the xcluster.")
	listXClusterCmd.MarkFlagRequired("target-universe-name")
}
