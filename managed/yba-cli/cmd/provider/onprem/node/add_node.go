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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem"
)

// addNodesCmd represents the provider command
var addNodesCmd = &cobra.Command{
	Use:   "add",
	Short: "Add a node instance to YugabyteDB Anywhere on-premises provider",
	Long:  "Add a node instance to YugabyteDB Anywhere on-premises provider",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to add node instance"+
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
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Node Instance", "Add - Fetch Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		if r[0].GetCode() != util.OnpremProviderType {
			errMessage := "Operation only supported for On-premises providers."
			logrus.Fatalf(formatter.Colorize(errMessage+"\n", formatter.RedColor))
		}

		providerUUID := r[0].GetUuid()

		instanceName, err := cmd.Flags().GetString("node-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		instanceType, err := cmd.Flags().GetString("instance-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		nodeIP, err := cmd.Flags().GetString("ip")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		region, err := cmd.Flags().GetString("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		regionUUID, err := fetchRegionUUIDFromRegionName(authAPI, providerUUID, region)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		zone, err := cmd.Flags().GetString("zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		azUUID, err := fetchZoneUUIDFromZoneName(authAPI, providerUUID, regionUUID, zone)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sshUser, err := cmd.Flags().GetString("ssh-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		nodeConfigs, err := cmd.Flags().GetStringArray("node-configs")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		node := ybaclient.NodeInstanceData{
			InstanceName: instanceName,
			InstanceType: instanceType,
			Ip:           nodeIP,
			NodeConfigs:  buildNodeConfig(nodeConfigs),
			Region:       region,
			Zone:         zone,
			SshUser:      sshUser,
		}
		nodeList := make([]ybaclient.NodeInstanceData, 0)
		nodeList = append(nodeList, node)

		requestBody := ybaclient.NodeInstanceFormData{
			Nodes: nodeList,
		}

		rCreate, response, err := authAPI.CreateNodeInstance(azUUID).
			NodeInstance(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance", "Add")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		nodesCtx := formatter.Context{
			Command: "add",
			Output:  os.Stdout,
			Format:  onprem.NewNodesFormat(viper.GetString("output")),
		}

		logrus.Infof("The node instance %s has been added to provider %s (%s)\n",
			formatter.Colorize(nodeIP, formatter.GreenColor),
			providerName,
			providerUUID)

		var nodeInstance ybaclient.NodeInstance
		for _, n := range rCreate {
			nodeInstance = n
		}

		nodeInstanceList := make([]ybaclient.NodeInstance, 0)
		nodeInstanceList = append(nodeInstanceList, nodeInstance)
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
			errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance", "Add - Fetch Universes")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		onprem.Write(nodesCtx, nodeInstanceList)

	},
}

func init() {
	addNodesCmd.Flags().SortFlags = false

	addNodesCmd.Flags().String("instance-type", "",
		"[Required] Instance type of the node as describe in the provider.")
	addNodesCmd.Flags().String("ip", "",
		"[Required] IP address of the node instance.")
	addNodesCmd.Flags().String("region", "",
		"[Required] Region name of the node.")
	addNodesCmd.Flags().String("zone", "",
		"[Required] Zone name of the node.")

	addNodesCmd.Flags().String("node-name", "",
		"[Optional] Node name given by the user.")
	addNodesCmd.Flags().String("ssh-user", "",
		"[Optional] SSH user to access the node instances.")
	addNodesCmd.Flags().StringArray("node-configs", []string{},
		"[Optional] Node configurations. Provide the following "+
			"comma separated fields as key-value pairs: "+
			"\"type=<type>,value=<value>\". Each config needs to be "+
			"added using a separate --node-configs flag. "+
			"Example: --node-configs type=S3CMD,value=<value> "+
			"--node-configs type=CPU_CORES,value=<value>")

	addNodesCmd.MarkFlagRequired("instance-type")
	addNodesCmd.MarkFlagRequired("ip")
	addNodesCmd.MarkFlagRequired("region")
	addNodesCmd.MarkFlagRequired("zone")

}

func fetchRegionUUIDFromRegionName(authAPI *ybaAuthClient.AuthAPIClient,
	providerUUID, regionName string) (string, error) {
	var err error
	r, response, err := authAPI.GetRegion(providerUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance",
			"Add - Fetch Regions")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		return "", errMessage
	}
	for _, region := range r {
		if region.GetName() == regionName {
			return region.GetUuid(), nil
		}
	}
	return "", fmt.Errorf("No region %s found in provider %s", regionName, providerUUID)
}

func fetchZoneUUIDFromZoneName(authAPI *ybaAuthClient.AuthAPIClient,
	providerUUID, regionUUID, azName string) (string, error) {
	r, response, err := authAPI.ListOfAZ(providerUUID, regionUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance",
			"Add - Fetch AZs")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		return "", errMessage
	}
	for _, az := range r {
		if az.GetName() == azName {
			return az.GetUuid(), nil
		}
	}
	return "", fmt.Errorf("No availability zone %s found in region "+
		" %s of provider %s", azName, regionUUID, providerUUID)
}

func buildNodeConfig(nodeConfigsStrings []string) *[]ybaclient.NodeConfig {
	if len(nodeConfigsStrings) == 0 {
		return nil
	}
	res := make([]ybaclient.NodeConfig, 0)
	for _, nodeConfigString := range nodeConfigsStrings {
		nodeConfig := map[string]string{}
		for _, nInfo := range strings.Split(nodeConfigString, ",") {
			kvp := strings.Split(nInfo, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in node config description.\n",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "type":
				if len(strings.TrimSpace(val)) != 0 {
					nodeConfig["type"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "value":
				if len(strings.TrimSpace(val)) != 0 {
					nodeConfig["value"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			}
		}
		if _, ok := nodeConfig["type"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Type not specified in node config.\n",
					formatter.RedColor))
		}
		if _, ok := nodeConfig["value"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Value not specified in node config.\n",
					formatter.RedColor))
		}

		r := ybaclient.NodeConfig{
			Type:  nodeConfig["type"],
			Value: nodeConfig["value"],
		}
		res = append(res, r)

	}
	return &res
}
