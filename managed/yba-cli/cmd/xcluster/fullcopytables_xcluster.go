/*
 * Copyright (c) YugabyteDB, Inc.
 */

package xcluster

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/xcluster"
)

var fullCopyXClusterCmd = &cobra.Command{
	Use: "needs-full-copy-tables",
	Aliases: []string{"needs-full-copy", "needs-fullcopy", "full-copy",
		"fullcopy", "bootstrap", "bootstrap-tables"},
	Short: "Check whether source universe tables need full copy " +
		"before setting up xCluster replication",
	Long: "Check whether source universe tables need full copy " +
		"before setting up xCluster replication",
	Example: `yba xcluster needs-full-copy-tables --source-universe-name <source-universe-name> \
	 --target-universe-name <target-universe-name> \
	 --table-uuids <uuid-1>,<uuid-2>,<uuid-3>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		sourceUniName, err := cmd.Flags().GetString("source-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(sourceUniName) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Missing source universe name\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
		operation := "Full Copy Tables"

		sourceUniverseName, err := cmd.Flags().GetString("source-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		targetUniverseName, err := cmd.Flags().GetString("target-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sourceUniverse, targetUniverse := GetSourceAndTargetXClusterUniverse(authAPI,
			sourceUniverseName, targetUniverseName, "", "", operation)

		sourceUniverseUUID := sourceUniverse.GetUniverseUUID()

		targetUniverseUUID := targetUniverse.GetUniverseUUID()

		tableUUIDsString, err := cmd.Flags().GetString("table-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tableUUIDsString = strings.TrimSpace(tableUUIDsString)
		tableUUIDs := make([]string, 0)
		if len(tableUUIDsString) != 0 {
			tableUUIDs = strings.Split(tableUUIDsString, ",")
		} else {
			rTables, response, err := authAPI.
				GetAllTables(sourceUniverseUUID).IncludeParentTableInfo(false).
				IncludeColocatedParentTables(true).
				XClusterSupportedOnly(true).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(
					response,
					err,
					"xCluster",
					fmt.Sprintf("%s - List Tables", operation))
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			if len(rTables) < 1 {
				logrus.Fatalf(
					formatter.Colorize(
						fmt.Sprintf("No tables found in source universe %s\n", sourceUniverseName),
						formatter.RedColor,
					))
			}

			for _, table := range rTables {
				tableUUIDs = append(tableUUIDs, table.GetTableUUID())
			}
		}

		req := ybaclient.XClusterConfigNeedBootstrapFormData{
			Tables:             tableUUIDs,
			TargetUniverseUUID: util.GetStringPointer(targetUniverseUUID),
		}

		r, response, err := authAPI.NeedBootstrapTable(sourceUniverseUUID).
			XclusterNeedBootstrapFormData(req).IncludeDetails(true).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "xCluster", operation)
		}

		xclusterNeedBootstrapCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  xcluster.NewFullCopyTableFormat(viper.GetString("output")),
		}

		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				if !util.IsEmptyString(targetUniverseName) {
					logrus.Infof(
						"No tables from source universe %s to target universe %s need full copy\n",
						sourceUniverseName, targetUniverseName)
				} else {
					logrus.Infof("No tables from source universe %s need full copy\n", sourceUniverseName)
				}
			} else {
				logrus.Info("[]\n")
			}
			return
		}

		tablesNeedBootstrap := make([]util.XClusterConfigNeedBootstrapResponse, 0)

		for tableID, info := range r {
			needBootstrapInfoByte, err := json.Marshal(info)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			var needBootstrapInfo util.XClusterConfigNeedBootstrapPerTableResponse

			err = json.Unmarshal(needBootstrapInfoByte, &needBootstrapInfo)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			tablesNeedBootstrap = append(
				tablesNeedBootstrap,
				util.XClusterConfigNeedBootstrapResponse{
					TableUUID:     tableID,
					BootstrapInfo: &needBootstrapInfo,
				},
			)
		}

		xcluster.FullCopyTableWrite(xclusterNeedBootstrapCtx, tablesNeedBootstrap)

	},
}

func init() {
	fullCopyXClusterCmd.Flags().SortFlags = false

	fullCopyXClusterCmd.Flags().String("source-universe-name", "",
		"[Required] The name of the source universe for the xcluster.")
	fullCopyXClusterCmd.MarkFlagRequired("source-universe-name")
	fullCopyXClusterCmd.Flags().String("target-universe-name", "",
		"[Optional] The name of the target universe for the xcluster. "+
			"If tables do not exist on the target universe, full copy is required. "+
			"If not specified, only source table is checked for data.")

	fullCopyXClusterCmd.Flags().String("table-uuids", "",
		"[Optional] The IDs/UUIDs of the source universe tables "+
			"that need to checked for full copy. "+
			"If left empty, all tables on the source universe will be checked.")
}
