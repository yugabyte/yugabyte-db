/*
 * Copyright (c) YugaByte, Inc.
 */

package xcluster

import (
	"fmt"
	"os"
	"strings"

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
		if len(strings.TrimSpace(sourceUniName)) == 0 || len(strings.TrimSpace(targetUniName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Missing source or target universe name\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
		universeListRequest := authAPI.ListUniverses()

		sourceUniName, err := cmd.Flags().GetString("source-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sourceUniverseList, response, err := universeListRequest.Name(sourceUniName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "XCluster", "List - Get Source Universe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(sourceUniverseList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", sourceUniName),
					formatter.RedColor,
				))
		}
		sourceUniverse := sourceUniverseList[0]

		targetUniName, err := cmd.Flags().GetString("target-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		targetUniniverseList, response, err := universeListRequest.Name(targetUniName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "XCluster", "List - Get Target Universe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(targetUniniverseList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", targetUniName),
					formatter.RedColor,
				))
		}
		targetUniverse := targetUniniverseList[0]

		sourceDetails := sourceUniverse.GetUniverseDetails()
		sourceXClusterConfigList := sourceDetails.GetSourceXClusterConfigs()
		if len(sourceXClusterConfigList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No source xcluster found in universe: %s\n", sourceUniName),
					formatter.RedColor,
				))
		}

		targetDetails := targetUniverse.GetUniverseDetails()
		targetXClusterConfigList := targetDetails.GetTargetXClusterConfigs()
		if len(targetXClusterConfigList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No target xcluster found in universe: %s\n", targetUniName),
					formatter.RedColor,
				))
		}

		xclusterUUIDs := util.FindCommonStringElements(sourceXClusterConfigList, targetXClusterConfigList)
		if len(xclusterUUIDs) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No xclusters found between universes %s and %s\n", sourceUniName, targetUniName),
					formatter.RedColor,
				))
		}

		r := make([]ybaclient.XClusterConfigGetResp, 0)
		for _, x := range xclusterUUIDs {
			rGet, response, err := authAPI.GetXClusterConfig(x).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "XCluster", "List")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
			r = append(r, rGet)
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
