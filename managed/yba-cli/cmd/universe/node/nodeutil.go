/*
 * Copyright (c) YugaByte, Inc.
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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
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
		fmt.Println("No universes found")
		return
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
	primaryCluster := clusters[0]
	userIntent := primaryCluster.GetUserIntent()
	if userIntent.GetProviderType() == util.K8sProviderType {
		errMessage := "Node operations are blocked for Kubernetes universe"
		logrus.Fatalf(formatter.Colorize(errMessage+"\n", formatter.RedColor))
	}
	nodeName, err := cmd.Flags().GetString("node-name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(nodeName)) == 0 {
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
	rOperation, response, err := nodeActionAPI.Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Node", operation)
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	taskUUID := rOperation.GetTaskUUID()

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
		fmt.Printf("The node %s operation %s has been completed\n",
			formatter.Colorize(nodeName, formatter.GreenColor), operation)

		nodesCtx := formatter.Context{
			Output: os.Stdout,
			Format: universe.NewNodesFormat(viper.GetString("output")),
		}

		nodeInstance, response, err := authAPI.GetNodeDetails(universeUUID, nodeName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Node",
				fmt.Sprintf("%s - Fetch Nodes", operation))
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		nodeInstanceList := make([]ybaclient.NodeDetailsResp, 0)
		nodeInstanceList = append(nodeInstanceList, nodeInstance)

		universe.NodeWrite(nodesCtx, nodeInstanceList)

	} else {
		fmt.Println(msg)
	}

}
