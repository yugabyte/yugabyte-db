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
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// preflightNodesCmd represents the provider command
var preflightNodesCmd = &cobra.Command{
	Use:     "preflight",
	Short:   "Preflight check a node of a YugabyteDB Anywhere on-premises provider",
	Long:    "Preflight check a node of a YugabyteDB Anywhere on-premises provider",
	Example: `yba provider onprem node preflight --name <provider-name> --ip <node-ip>`,
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
		providerListRequest = providerListRequest.Name(providerName).
			ProviderCode(util.OnpremProviderType)
		r, response, err := providerListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Node Instance", "Preflight - Fetch Provider")
		}
		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No on premises providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		providerUUID := r[0].GetUuid()

		nodesFromProvider, response, err := authAPI.ListByProvider(providerUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Node Instance", "Preflight - Fetch Nodes")
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
		rTask, response, err := detachedNodeActionAPI.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Node Instance", "Preflight")
		}

		util.CheckTaskAfterCreation(rTask)

		nodeUUID := rTask.GetResourceUUID()
		taskUUID := rTask.GetTaskUUID()

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
			logrus.Infof("The node %s (%s) has been checked\n",
				formatter.Colorize(ip, formatter.GreenColor), nodeUUID)

			nodesCtx := formatter.Context{
				Command: "preflight",
				Output:  os.Stdout,
				Format:  onprem.NewNodesFormat(viper.GetString("output")),
			}

			nodeInstance, response, err := authAPI.GetNodeInstance(nodeUUID).Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Node Instance", "Preflight - Fetch Nodes")
			}

			nodeInstanceList := util.CheckAndAppend(
				make([]ybaclient.NodeInstance, 0),
				nodeInstance,
				fmt.Sprintf("Node Instance %s not found", nodeUUID),
			)
			universeList, response, err := authAPI.ListUniverses().Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Node Instance", "Preflight - Fetch Universes")
			}
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

			onprem.Write(nodesCtx, nodeInstanceList)
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "preflight",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})

	},
}

func init() {
	preflightNodesCmd.Flags().SortFlags = false

	preflightNodesCmd.Flags().String("ip", "",
		"[Required] IP address of the node instance.")

	preflightNodesCmd.MarkFlagRequired("ip")
}
