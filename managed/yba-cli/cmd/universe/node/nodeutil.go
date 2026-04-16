/*
 * Copyright (c) YugabyteDB, Inc.
 */

package node

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

func nodeOperationsUtil(cmd *cobra.Command, operation, command string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	universeListRequest := authAPI.ListUniverses()

	universeName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if universeName != "" {
		universeListRequest = universeListRequest.Name(universeName)
	}

	r, response, err := universeListRequest.Execute()
	if err != nil {

		errMessage := util.ErrorFromHTTPResponse(
			response, err,
			"Node",
			fmt.Sprintf("%s - List Universes", operation))
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No universe with name: %s found\n", universeName),
				formatter.RedColor,
			))
	}

	universeInUse := r[0]
	universeUUID := universeInUse.GetUniverseUUID()
	universeDetails := universeInUse.GetUniverseDetails()
	clusters := universeDetails.GetClusters()
	if len(clusters) < 1 {
		err := fmt.Errorf(
			"No clusters found in universe " + universeName + " (" + universeUUID + ")")
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	primaryCluster := universeutil.FindClusterByType(clusters, util.PrimaryClusterType)

	if universeutil.IsClusterEmpty(primaryCluster) {
		err := fmt.Errorf(
			"No primary cluster found in universe " + universeName + " (" + universeUUID + ")")
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	userIntent := primaryCluster.GetUserIntent()
	if userIntent.GetProviderType() == util.K8sProviderType {
		errMessage := "Node operations are blocked for Kubernetes universe"
		logrus.Fatalf(formatter.Colorize(errMessage+"\n", formatter.RedColor))
	}
	nodeName, err := cmd.Flags().GetString("node-name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(nodeName) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize("No node name found to perform operation"+
				"\n", formatter.RedColor))
	}
	nodeActionAPI := authAPI.NodeAction(universeUUID, nodeName)
	nodeActionAPI = nodeActionAPI.NodeAction(
		ybaclient.NodeActionFormData{
			NodeAction: command,
		},
	)
	rTask, response, err := nodeActionAPI.Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "Node", operation)
	}
	util.CheckTaskAfterCreation(rTask)
	taskUUID := rTask.GetTaskUUID()

	msg := fmt.Sprintf("The node %s operation - %s is being performed in universe %s (%s)",
		formatter.Colorize(nodeName, formatter.GreenColor),
		operation,
		universeName, universeUUID)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(
				fmt.Sprintf("\nWaiting for node %s operation %s to be completed\n",
					formatter.Colorize(nodeName, formatter.GreenColor), operation))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The node %s operation %s has been completed\n",
			formatter.Colorize(nodeName, formatter.GreenColor), operation)

		nodesCtx := formatter.Context{
			Command: "edit",
			Output:  os.Stdout,
			Format:  universe.NewNodesFormat(viper.GetString("output")),
		}

		if !isNodeRemovingOperation(operation) {
			nodeInstance, response, err := authAPI.GetNodeDetails(universeUUID, nodeName).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Node",
					fmt.Sprintf("%s - Fetch Nodes", operation))
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			nodeInstanceList := util.CheckAndAppend(
				make([]ybaclient.NodeDetailsResp, 0),
				nodeInstance,
				fmt.Sprintf("Node %s not found", nodeName),
			)

			universe.NodeWrite(nodesCtx, nodeInstanceList)
			return
		}
		nodesCtx.Command = "list"

		r, response, err := universeListRequest.Execute()
		if err != nil {

			errMessage := util.ErrorFromHTTPResponse(
				response, err,
				"Node",
				fmt.Sprintf("%s - List Universes", operation))
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		selectedUniverse := r[0]
		details := selectedUniverse.GetUniverseDetails()
		nodes := details.GetNodeDetailsSet()
		universe.NodeWrite(nodesCtx, nodes)
		return
	}

	logrus.Infoln(msg + "\n")

	taskCtx := formatter.Context{
		Command: "edit",
		Output:  os.Stdout,
		Format:  ybatask.NewTaskFormat(viper.GetString("output")),
	}
	ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})

}

func isNodeRemovingOperation(operation string) bool {
	operation = strings.ToLower(operation)
	return strings.EqualFold(operation, "replace") || strings.EqualFold(operation, "decommission")
}
