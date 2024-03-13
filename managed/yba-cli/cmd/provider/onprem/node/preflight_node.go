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
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem"
)

// preflightNodesCmd represents the provider command
var preflightNodesCmd = &cobra.Command{
	Use:   "preflight",
	Short: "Preflight check a node of a YugabyteDB Anywhere on-premises provider",
	Long:  "Preflight check a node of a YugabyteDB Anywhere on-premises provider",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to preflight check node"+
					"\n", formatter.RedColor))
		}
		ip, err := cmd.Flags().GetString("ip")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(ip) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No node name found to preflight check"+
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
				"Node Instance", "Preflight - Fetch Provider")
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

		nodesFromProvider, response, err := authAPI.ListByProvider(providerUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance",
				"Preflight - Fetch Nodes")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		ip, err := cmd.Flags().GetString("ip")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var preflightNode ybaclient.NodeInstance
		for _, n := range nodesFromProvider {
			details := n.GetDetails()
			if details.GetIp() == ip {
				preflightNode = n
			}
		}

		detachedNodeActionAPI := authAPI.DetachedNodeAction(providerUUID, ip)
		detachedNodeActionAPI = detachedNodeActionAPI.NodeAction(
			ybaclient.NodeActionFormData{
				NodeAction: "PRECHECK_DETACHED",
			},
		)
		rPreflight, response, err := detachedNodeActionAPI.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance", "Preflight")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		nodeUUID := rPreflight.GetResourceUUID()
		taskUUID := rPreflight.GetTaskUUID()

		if len(nodeUUID) == 0 {
			nodeUUID = preflightNode.GetNodeUuid()
		}

		msg := fmt.Sprintf("The node %s is being checked",
			formatter.Colorize(ip, formatter.GreenColor))

		if viper.GetBool("wait") {
			if taskUUID != "" {
				logrus.Info(fmt.Sprintf("\nWaiting for node %s (%s) to be checked\n",
					formatter.Colorize(ip, formatter.GreenColor), nodeUUID))
				err = authAPI.WaitForTask(taskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			fmt.Printf("The node %s (%s) has been checked\n",
				formatter.Colorize(ip, formatter.GreenColor), nodeUUID)

			nodesCtx := formatter.Context{
				Output: os.Stdout,
				Format: onprem.NewNodesFormat(viper.GetString("output")),
			}

			nodeInstance, response, err := authAPI.GetNodeInstance(nodeUUID).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance",
					"Preflight - Fetch Nodes")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			nodeInstanceList := make([]ybaclient.NodeInstance, 0)
			nodeInstanceList = append(nodeInstanceList, nodeInstance)
			universeList, response, err := authAPI.ListUniverses().Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance",
					"Preflight - Fetch Universes")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
			for _, u := range universeList {
				details := u.GetUniverseDetails()
				primaryCluster := details.GetClusters()[0]
				userIntent := primaryCluster.GetUserIntent()
				if userIntent.GetProvider() == providerUUID {
					onprem.UniverseList = append(onprem.UniverseList, u)
				}
			}

			onprem.Write(nodesCtx, nodeInstanceList)

		} else {
			fmt.Println(msg)
		}
	},
}

func init() {
	preflightNodesCmd.Flags().SortFlags = false

	preflightNodesCmd.Flags().String("ip", "",
		"[Required] IP address of the node instance.")

	preflightNodesCmd.MarkFlagRequired("ip")
}
