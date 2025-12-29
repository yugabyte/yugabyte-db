/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cluster

import (
	"fmt"
	"net/http"
	"os"
	"slices"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	universeFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

func editClusterUtil(
	cmd *cobra.Command,
	clusterType string,

) (*ybaAuthClient.AuthAPIClient,
	ybaclient.UniverseResp,
	ybaclient.UniverseConfigureTaskParams,
	error) {
	authAPI, universe, err := universeutil.Validations(cmd, util.EditOperation)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	universeUUID := universe.GetUniverseUUID()
	universeName := universe.GetName()
	universeDetails := universe.GetUniverseDetails()
	clusters := universeDetails.GetClusters()
	if len(clusters) < 1 {
		err := fmt.Errorf(
			"No clusters found in universe " + universeName + " (" + universeUUID + ")")
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}

	var cluster ybaclient.Cluster
	if strings.Compare(clusterType, util.ReadReplicaCluster) == 0 {
		if len(clusters) < 2 {
			err := fmt.Errorf(
				"No read replica clusters found in universe " +
					universeName + " (" + universeUUID +
					"). Use command \"yba edit-cluster primary\" to edit primary cluster.")
			return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
		}
	}

	clusterIndex := 0
	for i, c := range clusters {
		if strings.EqualFold(c.GetClusterType(), clusterType) {
			cluster = c
			clusterIndex = i
			break
		}
	}

	if universeutil.IsClusterEmpty(cluster) {
		err := fmt.Errorf(
			"No cluster found with type " + clusterType + " in universe " +
				universeName + " (" + universeUUID + ")")
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}

	userIntent := cluster.GetUserIntent()

	providerUUID := userIntent.GetProvider()
	providerUsed, response, err := authAPI.GetProvider(providerUUID).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "Universe", "Edit Cluster - Fetch Provider")
	}
	regionsInProvider := providerUsed.GetRegions()

	removeRegionsInput, err := cmd.Flags().GetString("remove-regions")
	if err != nil {
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}
	if !util.IsEmptyString(removeRegionsInput) {
		regions, err := universeutil.FetchRegionUUIDFromName(
			regionsInProvider,
			removeRegionsInput,
			"remove",
		)
		if err != nil {
			return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
		}
		regionsInUniverse := userIntent.GetRegionList()
		regionsLeftAfterRemoving := make([]string, 0)
		for _, r := range regionsInUniverse {
			if !slices.Contains(regions, r) {
				regionsLeftAfterRemoving = append(regionsLeftAfterRemoving, r)
			}
		}
		userIntent.SetRegionList(regionsLeftAfterRemoving)
	}

	addRegionsInput, err := cmd.Flags().GetString("add-regions")
	if err != nil {
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}

	if !util.IsEmptyString(addRegionsInput) {
		regions, err := universeutil.FetchRegionUUIDFromName(
			regionsInProvider,
			addRegionsInput,
			"add",
		)
		if err != nil {
			return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
		}
		regionsInUniverse := userIntent.GetRegionList()
		for _, r := range regions {
			if !slices.Contains(regionsInUniverse, r) {
				regionsInUniverse = append(regionsInUniverse, r)
			}
		}
		userIntent.SetRegionList(regionsInUniverse)
	}

	removeZones, err := cmd.Flags().GetStringArray("remove-zones")
	if err != nil {
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}
	editZones, err := cmd.Flags().GetStringArray("edit-zones")
	if err != nil {
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}
	addZones, err := cmd.Flags().GetStringArray("add-zones")
	if err != nil {
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}

	placementInfo := cluster.GetPlacementInfo()
	cloudList := placementInfo.GetCloudList()
	cloudList, err = universeutil.RemoveOrAddZones(
		cloudList,
		providerUsed.GetRegions(),
		providerUsed.GetUuid(),
		userIntent.GetRegionList(),
		removeZones,
		editZones,
		addZones,
	)
	if err != nil {
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}

	placementInfo.SetCloudList(cloudList)
	cluster.SetPlacementInfo(placementInfo)

	if cmd.Flags().Changed("num-nodes") {
		numNode, err := cmd.Flags().GetInt("num-nodes")
		if err != nil {
			return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
		}

		if int32(numNode) != userIntent.GetNumNodes() {
			userIntent.SetNumNodes(int32(numNode))
		}
	}

	deviceInfo, err := deviceInfoChanges(cmd, userIntent)
	if err != nil {
		return nil, ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}

	if strings.Compare(clusterType, util.PrimaryCluster) == 0 {
		if cmd.Flags().Changed("dedicated-nodes") {
			dedicatedNodes, err := cmd.Flags().GetBool("dedicated-nodes")
			if err != nil {
				return nil,
					ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
			}
			userIntent.SetDedicatedNodes(dedicatedNodes)
		}
		dedicatedNodes := userIntent.GetDedicatedNodes()
		if dedicatedNodes {
			masterDeviceInfo := userIntent.GetMasterDeviceInfo()
			// check if empty, set to userIntent deviceInfo
			if universeutil.IsDeviceInfoEmpty(masterDeviceInfo) {
				masterDeviceInfo = userIntent.GetDeviceInfo()
			}
			masterInstanceType, err := cmd.Flags().GetString("dedicated-master-instance-type")
			if err != nil {
				return nil,
					ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
			}

			if !util.IsEmptyString(masterInstanceType) {
				if strings.Compare(masterInstanceType, userIntent.GetMasterInstanceType()) != 0 {
					userIntent.SetMasterInstanceType(masterInstanceType)
				}
				if cmd.Flags().Changed("dedicated-master-num-volumes") {
					masterNumVolume, err := cmd.Flags().GetInt("dedicated-master-num-volumes")
					if err != nil {
						return nil,
							ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
					}
					masterDeviceInfo.SetNumVolumes(int32(masterNumVolume))
				}
			}

			if cmd.Flags().Changed("dedicated-master-volume-size") {
				masterVolumeSize, err := cmd.Flags().GetInt("dedicated-master-volume-size")
				if err != nil {
					return nil,
						ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
				}

				masterDeviceInfo.SetVolumeSize(int32(masterVolumeSize))
			}

			masterStorageType, err := cmd.Flags().GetString("dedicated-master-storage-type")
			if err != nil {
				return nil,
					ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
			}
			if !util.IsEmptyString(masterStorageType) {
				masterDeviceInfo.SetStorageType(masterStorageType)
			}

			masterStorageClass, err := cmd.Flags().GetString("dedicated-master-storage-class")
			if err != nil {
				return nil,
					ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
			}
			if !util.IsEmptyString(masterStorageClass) {
				masterDeviceInfo.SetStorageClass(masterStorageClass)
			}
			userIntent.SetMasterDeviceInfo(masterDeviceInfo)
		}
	} else {
		if cmd.Flags().Changed("replication-factor") {
			rf, err := cmd.Flags().GetInt("replication-factor")
			if err != nil {
				return nil,
					ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
			}
			userIntent.SetReplicationFactor(int32(rf))
		}
	}

	// usertags
	userTags := userIntent.GetInstanceTags()
	userTags, err = editUserTags(cmd, userTags)
	if err != nil {
		return nil,
			ybaclient.UniverseResp{}, ybaclient.UniverseConfigureTaskParams{}, err
	}
	userIntent.SetInstanceTags(userTags)

	userIntent.SetDeviceInfo(deviceInfo)
	cluster.SetUserIntent(userIntent)
	clusters[clusterIndex] = cluster

	req := ybaclient.UniverseConfigureTaskParams{
		UniverseUUID: util.GetStringPointer(universeUUID),
		Clusters:     clusters,
		NodeDetailsSet: universeutil.BuildNodeDetailsRespArrayToNodeDetailsArray(
			universeDetails.GetNodeDetailsSet()),
		CommunicationPorts: universeDetails.CommunicationPorts,
	}

	return authAPI, universe, req, nil
}

func editUserTags(cmd *cobra.Command, userTags map[string]string) (map[string]string, error) {
	addUserTagsMap, err := cmd.Flags().GetStringToString("add-user-tags")
	if err != nil {
		return nil, err
	}
	for k, v := range addUserTagsMap {
		if _, exists := userTags[k]; exists {
			logrus.Info(
				fmt.Sprintf("%s already exists in User Tags, ignoring", k),
			)
		} else {
			userTags[k] = v
		}
	}
	editUserTagsMap, err := cmd.Flags().GetStringToString("edit-user-tags")
	if err != nil {
		return nil, err
	}
	for k, v := range editUserTagsMap {
		if _, exists := userTags[k]; !exists {
			logrus.Debug(
				fmt.Sprintf("%s already exists in User Tags, ignoring", k),
			)
		} else {
			userTags[k] = v
		}
	}
	removeUserTags, err := cmd.Flags().GetString("remove-user-tags")
	if err != nil {
		return nil, err
	}
	if !util.IsEmptyString(removeUserTags) {
		for _, k := range strings.Split(removeUserTags, ",") {
			if _, exists := userTags[k]; !exists {
				logrus.Debug(
					fmt.Sprintf("%s does not exist in user tags to remove, ignoring", k))
			} else {
				delete(userTags, k)
			}
		}
	}
	return userTags, nil
}

func deviceInfoChanges(
	cmd *cobra.Command,
	userIntent ybaclient.UserIntent,
) (ybaclient.DeviceInfo, error) {
	deviceInfo := userIntent.GetDeviceInfo()
	instanceType, err := cmd.Flags().GetString("instance-type")
	if err != nil {
		return ybaclient.DeviceInfo{}, err
	}
	if !util.IsEmptyString(instanceType) {
		if strings.Compare(instanceType, userIntent.GetInstanceType()) != 0 {
			userIntent.SetInstanceType(instanceType)
		}
		if cmd.Flags().Changed("num-volumes") {
			numVolume, err := cmd.Flags().GetInt("num-volumes")
			if err != nil {
				return ybaclient.DeviceInfo{}, err
			}

			deviceInfo.SetNumVolumes(int32(numVolume))
		}
	}

	storageType, err := cmd.Flags().GetString("storage-type")
	if err != nil {
		return ybaclient.DeviceInfo{}, err
	}

	if !util.IsEmptyString(storageType) {
		deviceInfo.SetStorageType(storageType)
	}

	storageClass, err := cmd.Flags().GetString("storage-class")
	if err != nil {
		return ybaclient.DeviceInfo{}, err
	}
	if !util.IsEmptyString(storageClass) {
		deviceInfo.SetStorageClass(storageClass)
	}

	if cmd.Flags().Changed("volume-size") {
		volumeSize, err := cmd.Flags().GetInt("volume-size")
		if err != nil {
			return ybaclient.DeviceInfo{}, err
		}
		deviceInfo.SetVolumeSize(int32(volumeSize))
	}
	return deviceInfo, nil
}

func waitForEditClusterTask(
	authAPI *ybaAuthClient.AuthAPIClient, universeName, universeUUID string,
	task *ybaclient.YBPTask,
) {
	var universeData []ybaclient.UniverseResp
	var response *http.Response
	var err error

	util.CheckTaskAfterCreation(task)

	taskUUID := task.GetTaskUUID()
	msg := fmt.Sprintf("The universe %s (%s) cluster is being edited",
		formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf("Waiting for universe %s (%s) to be edited\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The universe %s (%s) cluster has been edited\n",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
		universeData, response, err = authAPI.ListUniverses().Name(universeName).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Upgrade - Fetch Universe")
		}
		universesCtx := formatter.Context{
			Output: os.Stdout,
			Format: universeFormatter.NewUniverseFormat(viper.GetString("output")),
		}

		universeFormatter.Write(universesCtx, universeData)
		return
	}
	logrus.Infoln(msg + "\n")
	taskCtx := formatter.Context{
		Command: "create",
		Output:  os.Stdout,
		Format:  ybatask.NewTaskFormat(viper.GetString("output")),
	}
	ybatask.Write(taskCtx, []ybaclient.YBPTask{*task})
}
