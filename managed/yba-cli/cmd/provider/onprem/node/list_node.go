/*
 * Copyright (c) YugaByte, Inc.
 */

package node

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem"
)

// listNodesCmd represents the provider command
var listNodesCmd = &cobra.Command{
	Use:   "list",
	Short: "List node instances of a YugabyteDB Anywhere on-premises provider",
	Long:  "List node instance of a YugabyteDB Anywhere on-premises provider",
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
		providerListRequest = providerListRequest.Name(providerName)
		r, response, err := providerListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Node Instance", "List - Fetch Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(r) < 1 {
			fmt.Println("No providers found\n")
			return
		}

		if r[0].GetCode() != util.OnpremProviderType {
			errMessage := "Operation only supported for On-premises providers."
			logrus.Fatalf(formatter.Colorize(errMessage+"\n", formatter.RedColor))
		}

		providerUUID := r[0].GetUuid()

		rList, response, err := authAPI.ListByProvider(providerUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance", "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		nodeInstancesCtx := formatter.Context{
			Output: os.Stdout,
			Format: onprem.NewNodesFormat(viper.GetString("output")),
		}
		if len(rList) < 1 {
			fmt.Println("No node instances found")
			return
		}
		universeList, response, err := authAPI.ListUniverses().Execute()
		for _, u := range universeList {
			details := u.GetUniverseDetails()
			primaryCluster := details.GetClusters()[0]
			userIntent := primaryCluster.GetUserIntent()
			if userIntent.GetProvider() == providerUUID {
				onprem.UniverseList = append(onprem.UniverseList, u)
			}
		}
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Node Instance", "List - Fetch Universes")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		onprem.Write(nodeInstancesCtx, rList)

	},
}

func init() {
	listNodesCmd.Flags().SortFlags = false
}
