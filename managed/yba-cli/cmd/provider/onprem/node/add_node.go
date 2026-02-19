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
)

// addNodesCmd represents the provider command
var addNodesCmd = &cobra.Command{
	Use:     "add",
	Aliases: []string{"create"},
	Short:   "Add a node instance to YugabyteDB Anywhere on-premises provider",
	Long:    "Add a node instance to YugabyteDB Anywhere on-premises provider",
	Example: `yba provider onprem add --name <provider-name> \
	--ip <node-ip> --instance-type <instance-type> \
	--region <region> --zone <zone>`,
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
		providerListRequest = providerListRequest.Name(providerName).
			ProviderCode(util.OnpremProviderType)
		r, response, err := providerListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Node Instance", "Add - Fetch Provider")
		}
		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No on premises providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
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

		rInstanceType, response, err := authAPI.InstanceTypeDetail(
			providerUUID,
			instanceType).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Node Instance", "Add - Fetch Instance Type")
		}

		if !rInstanceType.GetActive() {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"Instance type: %s is unavailable or not active in provider %s\n",
						instanceType, providerName),
					formatter.RedColor,
				))
		}

		nodeIP, err := cmd.Flags().GetString("ip")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		region, err := cmd.Flags().GetString("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		regionUUID, err := fetchRegionUUIDFromRegionName(
			authAPI,
			providerName,
			providerUUID,
			region,
		)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		zone, err := cmd.Flags().GetString("zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		azUUID, err := fetchZoneUUIDFromZoneName(
			authAPI, providerName, providerUUID, region, regionUUID, zone)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sshUser, err := cmd.Flags().GetString("ssh-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		node := ybaclient.NodeInstanceData{
			InstanceName: instanceName,
			InstanceType: instanceType,
			Ip:           nodeIP,
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
			util.FatalHTTPError(response, err, "Node Instance", "Add")
		}

		util.CheckAndDereference(rCreate, "Failed to add node instance to provider")

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
		for _, n := range *rCreate {
			nodeInstance = n
		}

		nodeInstanceList := make([]ybaclient.NodeInstance, 0)
		nodeInstanceList = append(nodeInstanceList, nodeInstance)
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
			util.FatalHTTPError(response, err, "Node Instance", "Add - Fetch Universes")
		}
		onprem.Write(nodesCtx, nodeInstanceList)

	},
}

func init() {
	addNodesCmd.Flags().SortFlags = false

	addNodesCmd.Flags().String("instance-type", "",
		"[Required] Instance type of the node as described in the provider. "+
			"Run \"yba provider onprem instance-type list "+
			"--name <provider-name>\" for a list of "+
			"instance types associated with the provider.")
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

	addNodesCmd.MarkFlagRequired("instance-type")
	addNodesCmd.MarkFlagRequired("ip")
	addNodesCmd.MarkFlagRequired("region")
	addNodesCmd.MarkFlagRequired("zone")

}

func fetchRegionUUIDFromRegionName(authAPI *ybaAuthClient.AuthAPIClient,
	providerName,
	providerUUID, regionName string) (string, error) {
	var err error
	r, response, err := authAPI.GetRegion(providerUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"Node Instance",
			"Add - Fetch Regions",
		)
		return "", errMessage
	}
	for _, region := range r {
		if region.GetName() == regionName {
			return region.GetUuid(), nil
		}
	}
	return "",
		fmt.Errorf("No region %s found in provider %s (%s)", regionName, providerName, providerUUID)
}

func fetchZoneUUIDFromZoneName(authAPI *ybaAuthClient.AuthAPIClient,
	providerName,
	providerUUID,
	regionName,
	regionUUID, azName string) (string, error) {
	r, response, err := authAPI.ListOfAZ(providerUUID, regionUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Node Instance", "Add - Fetch AZs")
		return "", errMessage
	}
	for _, az := range r {
		if az.GetName() == azName {
			return az.GetUuid(), nil
		}
	}
	return "", fmt.Errorf(
		"No availability zone %s found in region %s (%s) of provider %s (%s)",
		azName, regionName, regionUUID, providerName, providerUUID)
}
