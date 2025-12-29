/*
 * Copyright (c) YugabyteDB, Inc.
 */

package node

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem"
)

// listNodesCmd represents the provider command
var listNodesCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List node instances of a YugabyteDB Anywhere on-premises provider",
	Long:    "List node instance of a YugabyteDB Anywhere on-premises provider",
	Example: `yba provider onprem node list --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to list node instances"+
					"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		providerListRequest := authAPI.GetListOfProviders()
		providerListRequest = providerListRequest.Name(providerName).
			ProviderCode(util.OnpremProviderType)
		r, response, err := providerListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Node Instance", "List - Fetch Provider")
		}
		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No on premises providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		providerUUID := r[0].GetUuid()

		rList, response, err := authAPI.ListByProvider(providerUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Node Instance", "List")
		}
		nodeInstancesCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  onprem.NewNodesFormat(viper.GetString("output")),
		}
		if len(rList) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No node instances found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		universeList, response, err := authAPI.ListUniverses().Execute()
		for _, u := range universeList {
			details := u.GetUniverseDetails()
			primaryCluster := universeutil.FindClusterByType(
				details.GetClusters(),
				util.PrimaryClusterType,
			)
			if universeutil.IsClusterEmpty(primaryCluster) {
				logrus.Debug(
					formatter.Colorize(
						fmt.Sprintf(
							"No primary cluster found in universe %s (%s)\n",
							u.GetName(),
							u.GetUniverseUUID(),
						),
						formatter.YellowColor,
					))
				continue
			}
			userIntent := primaryCluster.GetUserIntent()
			if userIntent.GetProvider() == providerUUID {
				onprem.UniverseList = append(onprem.UniverseList, u)
			}
		}
		if err != nil {
			util.FatalHTTPError(response, err, "Node Instance", "List - Fetch Universes")
		}
		onprem.Write(nodeInstancesCtx, rList)

	},
}

func init() {
	listNodesCmd.Flags().SortFlags = false
}
