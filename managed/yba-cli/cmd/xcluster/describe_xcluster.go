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

var describeXClusterCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere xcluster between two universes",
	Long:    "Describe a xcluster in YugabyteDB Anywhere between two universes",
	Example: `yba xcluster describe --uuid <xcluster-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(uuid) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No xcluster uuid found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rXCluster, response, err := authAPI.GetXClusterConfig(uuid).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "xCluster", "Describe")
		}

		xclusterConfig := util.CheckAndDereference(
			rXCluster,
			fmt.Sprintf("No xcluster found with uuid %s", uuid),
		)

		sourceUniverseUUID := xclusterConfig.GetSourceUniverseUUID()
		targetUniverseUUID := xclusterConfig.GetTargetUniverseUUID()

		sourceUniverse, response, err := authAPI.GetUniverse(sourceUniverseUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "xCluster", "Describe - Get Source Universe")
		}

		xcluster.SourceUniverse = util.CheckAndDereference(
			sourceUniverse,
			fmt.Sprintf("No source universe found with uuid %s", sourceUniverseUUID),
		)

		targetUniverse, response, err := authAPI.GetUniverse(targetUniverseUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "xCluster", "Describe - Get Target Universe")
		}

		xcluster.TargetUniverse = util.CheckAndDereference(
			targetUniverse,
			fmt.Sprintf("No target universe found with uuid %s", targetUniverseUUID),
		)

		r := make([]ybaclient.XClusterConfigGetResp, 0)

		r = append(r, xclusterConfig)

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullXClusterContext := *xcluster.NewFullXClusterContext()
			fullXClusterContext.Output = os.Stdout
			fullXClusterContext.Format = xcluster.NewFullXClusterFormat(viper.GetString("output"))
			fullXClusterContext.SetFullXCluster(r[0])
			fullXClusterContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No xclusters with uuid: %s found\n", uuid),
					formatter.RedColor,
				))
		}

		xclusterCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  xcluster.NewXClusterFormat(viper.GetString("output")),
		}
		xcluster.Write(xclusterCtx, r)

	},
}

func init() {
	describeXClusterCmd.Flags().SortFlags = false

	describeXClusterCmd.Flags().StringP("uuid", "u", "",
		"[Required] The uuid of the xcluster to get details.")
	describeXClusterCmd.MarkFlagRequired("uuid")
}
