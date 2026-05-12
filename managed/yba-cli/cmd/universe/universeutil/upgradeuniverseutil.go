/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universeutil

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"reflect"
	"slices"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	universeFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
	"gopkg.in/yaml.v2"
)

// WaitForUpgradeUniverseTask waits for the upgrade task to complete
func WaitForUpgradeUniverseTask(
	authAPI *ybaAuthClient.AuthAPIClient, universeName string, rTask *ybaclient.YBPTask) {

	var universeData []ybaclient.UniverseResp
	var response *http.Response
	var err error

	util.CheckTaskAfterCreation(rTask)

	universeUUID := rTask.GetResourceUUID()
	taskUUID := rTask.GetTaskUUID()

	msg := fmt.Sprintf("The universe %s (%s) is being upgraded",
		formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf("Waiting for universe %s (%s) to be upgraded\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The universe %s (%s) has been upgraded\n",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

		universeData, response, err = authAPI.ListUniverses().Name(universeName).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Upgrade - Fetch Universe")
		}
		universesCtx := formatter.Context{
			Command: "upgrade",
			Output:  os.Stdout,
			Format:  universeFormatter.NewUniverseFormat(viper.GetString("output")),
		}

		universeFormatter.Write(universesCtx, universeData)
		return
	}
	logrus.Infoln(msg + "\n")
	taskCtx := formatter.Context{
		Command: "upgrade",
		Output:  os.Stdout,
		Format:  ybatask.NewTaskFormat(viper.GetString("output")),
	}
	ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})

}

// Validations to ensure that the universe being accessed exists
func Validations(cmd *cobra.Command, operation string) (
	*ybaAuthClient.AuthAPIClient,
	ybaclient.UniverseResp,
	error,
) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	universeNameFlag := "name"
	if strings.EqualFold(operation, util.PITROperation) {
		universeNameFlag = "universe-name"
	}
	universeName, err := cmd.Flags().GetString(universeNameFlag)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	universeListRequest := authAPI.ListUniverses()
	universeListRequest = universeListRequest.Name(universeName)

	r, response, err := universeListRequest.Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err,
			"Universe",
			fmt.Sprintf("%s - List Universes", operation))
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	if len(r) < 1 {
		err := fmt.Errorf("No universes with name: %s found\n", universeName)
		return nil, ybaclient.UniverseResp{}, err
	}
	return authAPI, r[0], nil
}

// FetchMasterGFlags is to fetch list of master gflags
func FetchMasterGFlags(masterGFlagsString string) map[string]string {
	masterGFlags := make(map[string]interface{}, 0)
	if !util.IsEmptyString(masterGFlagsString) {
		for _, masterGFlagPair := range strings.Split(masterGFlagsString, ",") {
			kvp := strings.Split(masterGFlagPair, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in master gflag.\n",
						formatter.RedColor))
			}
			masterGFlags[kvp[0]] = kvp[1]
		}
	}
	return *util.StringMap(masterGFlags)
}

// FetchTServerGFlags is to fetch list of tserver gflags
func FetchTServerGFlags(
	tserverGFlagsStringList []string,
	noOfClusters int,
) []map[string]string {
	tserverGFlagsList := make([]map[string]string, 0)
	for _, tserverGFlagsString := range tserverGFlagsStringList {
		if !util.IsEmptyString(tserverGFlagsString) {
			tserverGFlags := make(map[string]interface{}, 0)
			for _, tserverGFlagPair := range strings.Split(tserverGFlagsString, ",") {
				kvp := strings.Split(tserverGFlagPair, "=")
				if len(kvp) != 2 {
					logrus.Fatalln(
						formatter.Colorize("Incorrect format in tserver gflag.\n",
							formatter.RedColor))
				}
				tserverGFlags[kvp[0]] = kvp[1]
			}
			tserverGflagsMap := util.StringMap(tserverGFlags)
			tserverGFlagsList = append(tserverGFlagsList, *tserverGflagsMap)
		}
	}
	if len(tserverGFlagsList) == 0 {
		for i := 0; i < noOfClusters; i++ {
			tserverGFlagsList = append(tserverGFlagsList, make(map[string]string, 0))
		}
	}
	tserverGFlagsListLen := len(tserverGFlagsList)
	if tserverGFlagsListLen < noOfClusters {
		for i := 0; i < noOfClusters-tserverGFlagsListLen; i++ {
			tserverGFlagsList = append(tserverGFlagsList, tserverGFlagsList[0])
		}
	}
	return tserverGFlagsList
}

// ProcessGFlagsJSONString takes in a JSON string and returns it as a map
func ProcessGFlagsJSONString(jsonData string, serverType string) map[string]string {
	// Parse the JSON input into a map
	var singleMap map[string]string
	if err := json.Unmarshal([]byte(jsonData), &singleMap); err != nil {
		logrus.Fatalf(formatter.Colorize(
			fmt.Sprintln("Error parsing JSON:", err), formatter.RedColor))
	}
	logrus.Debug(serverType+" GFlags from JSON string: ", singleMap)
	return singleMap
}

// ProcessGFlagsYAMLString takes in a YAML string and returns it as a map
func ProcessGFlagsYAMLString(yamlData string, serverType string) map[string]string {
	// Parse the YAML input into a map
	var singleMap map[string]string
	if err := yaml.Unmarshal([]byte(yamlData), &singleMap); err != nil {
		logrus.Fatalf(formatter.Colorize(
			fmt.Sprintln("Error parsing YAML:", err), formatter.RedColor))
	}
	logrus.Debug(serverType+" GFlags from YAML string: ", singleMap)
	return singleMap
}

// ProcessTServerGFlagsFromString takes in a string and returns it as a map
func ProcessTServerGFlagsFromString(input string, data interface{}) error {
	if err := json.Unmarshal([]byte(input), data); err == nil {
		logrus.Debug("Tserver GFlags from JSON string: ", data)
		return nil
	}

	if err := yaml.Unmarshal([]byte(input), data); err == nil {
		logrus.Debug("Tserver GFlags from YAML string: ", data)
		return nil
	}

	return fmt.Errorf("TServer GFlags is neither valid JSON nor valid YAML")
}

// ProcessTServerGFlagsFromConfig takes in a map from config file and returns it as a map
func ProcessTServerGFlagsFromConfig(input map[string]interface{}) map[string]map[string]string {
	data := make(map[string]map[string]string, 0)
	for k, v := range input {
		if reflect.TypeOf(v).Kind() != reflect.Map {
			logrus.Fatalf(formatter.Colorize(
				fmt.Sprintf("Invalid map entry for tserver key %s\n", k), formatter.RedColor))
		}
		data[k] = *util.StringMap(v.(map[string]interface{}))
	}
	return data
}

// BuildNodeDetailsRespArrayToNodeDetailsArray takes in an array of NodeDetailsResp and returns an array of NodeDetails
func BuildNodeDetailsRespArrayToNodeDetailsArray(
	nodes []ybaclient.NodeDetailsResp,
) []ybaclient.NodeDetails {
	var nodesDetails []ybaclient.NodeDetails
	for _, v := range nodes {
		nodeDetail := ybaclient.NodeDetails{
			AzUuid:                v.AzUuid,
			CloudInfo:             v.CloudInfo,
			CronsActive:           v.CronsActive,
			DisksAreMountedByUUID: v.DisksAreMountedByUUID,
			IsMaster:              v.IsMaster,
			IsRedisServer:         v.IsRedisServer,
			IsTserver:             v.IsTserver,
			IsYqlServer:           v.IsYqlServer,
			IsYsqlServer:          v.IsYsqlServer,
			MachineImage:          v.MachineImage,
			MasterHttpPort:        v.MasterHttpPort,
			MasterRpcPort:         v.MasterRpcPort,
			MasterState:           v.MasterState,
			NodeExporterPort:      v.NodeExporterPort,
			NodeIdx:               v.NodeIdx,
			NodeName:              v.NodeName,
			NodeUuid:              v.NodeUuid,
			PlacementUuid:         v.PlacementUuid,
			RedisServerHttpPort:   v.RedisServerHttpPort,
			RedisServerRpcPort:    v.RedisServerRpcPort,
			State:                 v.State,
			TserverHttpPort:       v.TserverHttpPort,
			TserverRpcPort:        v.TserverRpcPort,
			YbPrebuiltAmi:         v.YbPrebuiltAmi,
			YqlServerHttpPort:     v.YqlServerHttpPort,
			YqlServerRpcPort:      v.YqlServerRpcPort,
			YsqlServerHttpPort:    v.YsqlServerHttpPort,
			YsqlServerRpcPort:     v.YsqlServerRpcPort,
		}
		nodesDetails = append(nodesDetails, nodeDetail)
	}
	return nodesDetails
}

// FetchRegionUUIDFromName fetches the region UUID from the region name
func FetchRegionUUIDFromName(
	regionsInProvider []ybaclient.Region,
	regionsInput, operation string,
) ([]string, error) {
	regions := make([]string, 0)
	regionCodeList := make([]string, 0)
	regionCodeList = append(regionCodeList, strings.Split(regionsInput, ",")...)

	for _, r := range regionCodeList {
		if len(regionsInProvider) > 0 {
			for _, rInProvider := range regionsInProvider {
				if strings.Compare(r, rInProvider.GetCode()) == 0 {
					regions = append(regions, rInProvider.GetUuid())
				}
			}
		}
	}
	if len(regions) != len(regionCodeList) {
		err := fmt.Errorf("the provided region name to %s cannot be found", operation)
		return nil, err
	}
	return regions, nil
}

// RemoveOrAddZones removes or adds zones
func RemoveOrAddZones(
	cloudList []ybaclient.PlacementCloud,
	regionsInProvider []ybaclient.Region,
	providerUUID string,
	regionsInUniverse, removeZonesString, editZonesStrings, addZonesString []string,
) ([]ybaclient.PlacementCloud, error) {
	for i, cloud := range cloudList {
		if cloud.GetUuid() != providerUUID {
			continue
		}
		remainingCloudListAfterRegionRemoved := make([]ybaclient.PlacementRegion, 0)
		regionListInCloud := cloud.GetRegionList()
		for _, region := range regionListInCloud {
			if slices.Contains(regionsInUniverse, region.GetUuid()) {
				remainingCloudListAfterRegionRemoved = append(
					remainingCloudListAfterRegionRemoved,
					region,
				)
			}
		}
		cloud.SetRegionList(remainingCloudListAfterRegionRemoved)
		if len(removeZonesString) != 0 {
			for _, removeZoneString := range removeZonesString {
				removeZone := providerutil.BuildZoneMapFromString(removeZoneString, "remove")
				regions := cloud.GetRegionList()
				for ri, region := range regions {
					if strings.EqualFold(removeZone["region-name"], region.GetCode()) {
						zonesInCloudRegion := region.GetAzList()
						remainingZonesInCloudRegion := make([]ybaclient.PlacementAZ, 0)
						for _, zone := range zonesInCloudRegion {
							if !strings.EqualFold(zone.GetName(), removeZone["name"]) {
								remainingZonesInCloudRegion = append(
									remainingZonesInCloudRegion,
									zone,
								)
							}
						}
						region.SetAzList(remainingZonesInCloudRegion)
						regions[ri] = region
					}
				}
				cloud.SetRegionList(regions)
			}
		}
		if len(editZonesStrings) != 0 {
			for _, editZoneString := range editZonesStrings {
				editZone := providerutil.BuildZoneMapFromString(editZoneString, "edit")
				regions := cloud.GetRegionList()
				for ri, region := range regions {
					if strings.EqualFold(editZone["region-name"], region.GetCode()) {
						zonesInCloudRegion := region.GetAzList()
						for zi, zone := range zonesInCloudRegion {
							if strings.EqualFold(zone.GetName(), editZone["name"]) {
								if editZone["num-nodes"] != "" {
									numNodes, err := strconv.Atoi(editZone["num-nodes"])
									if err != nil {
										return nil, err
									}
									zone.SetNumNodesInAZ(int32(numNodes))
									zonesInCloudRegion[zi] = zone
								}
							}
						}
						region.SetAzList(zonesInCloudRegion)
						regions[ri] = region
					}
				}
				cloud.SetRegionList(regions)
			}
		}
		if len(addZonesString) != 0 {
			for _, addZoneString := range addZonesString {
				addZone := providerutil.BuildZoneMapFromString(addZoneString, "add")
				addZoneName := addZone["name"]
				addZoneRegion := addZone["region-name"]
				addNumNodes, err := strconv.Atoi(addZone["num-nodes"])
				if err != nil {
					return nil, err
				}

				var regionOfWhichZoneToBeAdded ybaclient.Region

				for _, regionInProvider := range regionsInProvider {
					if strings.EqualFold(regionInProvider.GetCode(), addZoneRegion) {
						regionOfWhichZoneToBeAdded = regionInProvider
						break
					}
				}
				if regionOfWhichZoneToBeAdded.GetUuid() == "" {
					return nil, fmt.Errorf("Region %s not found in provider", addZoneRegion)
				}

				if !slices.Contains(regionsInUniverse, regionOfWhichZoneToBeAdded.GetUuid()) {
					return nil, fmt.Errorf(
						"Region %s not found in universe, add using --add-regions/--regions flag",
						addZoneRegion,
					)
				}

				zoneToBeAdded := ybaclient.AvailabilityZone{}
				for _, zoneInRegion := range regionOfWhichZoneToBeAdded.GetZones() {
					if strings.EqualFold(zoneInRegion.GetName(), addZoneName) {
						zoneToBeAdded = zoneInRegion
						break
					}
				}
				if zoneToBeAdded.GetName() == "" {
					return nil, fmt.Errorf(
						"Zone %s not found in provider region %s",
						addZoneName,
						addZoneRegion,
					)
				}

				regionFoundInCloudList := false
				regions := cloud.GetRegionList()
				for ri, region := range regions {
					if strings.EqualFold(region.GetUuid(), regionOfWhichZoneToBeAdded.GetUuid()) {
						regionFoundInCloudList = true
						zonesInCloudRegion := region.GetAzList()
						for _, zone := range zonesInCloudRegion {
							if strings.EqualFold(zone.GetName(), addZoneName) {
								return nil, fmt.Errorf(
									"Zone %s already exists in region %s, use --edit-zones to edit number of zones",
									addZoneName,
									addZoneRegion,
								)
							}
						}
						zoneToAdd := ybaclient.PlacementAZ{
							Name:            util.GetStringPointer(addZoneName),
							NumNodesInAZ:    util.GetInt32Pointer(int32(addNumNodes)),
							Subnet:          zoneToBeAdded.Subnet,
							Uuid:            zoneToBeAdded.Uuid,
							SecondarySubnet: zoneToBeAdded.SecondarySubnet,
						}
						zonesInCloudRegion = append(zonesInCloudRegion, zoneToAdd)

						region.SetAzList(zonesInCloudRegion)
						regions[ri] = region
					}
				}
				cloud.SetRegionList(regions)
				if !regionFoundInCloudList {
					regionToAdd := ybaclient.PlacementRegion{
						Name: regionOfWhichZoneToBeAdded.Name,
						Code: regionOfWhichZoneToBeAdded.Code,
						Uuid: regionOfWhichZoneToBeAdded.Uuid,
					}
					zoneToAdd := ybaclient.PlacementAZ{
						Name:            util.GetStringPointer(zoneToBeAdded.Name),
						NumNodesInAZ:    util.GetInt32Pointer(int32(addNumNodes)),
						Subnet:          zoneToBeAdded.Subnet,
						Uuid:            zoneToBeAdded.Uuid,
						SecondarySubnet: zoneToBeAdded.SecondarySubnet,
					}
					regionToAdd.SetAzList([]ybaclient.PlacementAZ{zoneToAdd})
					regions := cloud.GetRegionList()
					regions = append(regions, regionToAdd)
					cloud.SetRegionList(regions)
				}
			}
		}
		cloudList[i] = cloud
	}
	return cloudList, nil
}

// FindClusterByType finds the cluster by type
func FindClusterByType(clusters []ybaclient.Cluster, clusterType string) ybaclient.Cluster {
	for _, cluster := range clusters {
		if strings.EqualFold(cluster.GetClusterType(), clusterType) {
			return cluster
		}
	}
	logrus.Debug("No cluster found with type: ", clusterType)
	return ybaclient.Cluster{}
}

// IsClusterEmpty checks if a cluster is empty/not found
func IsClusterEmpty(cluster ybaclient.Cluster) bool {
	return cluster.GetClusterType() == ""
}

// IsDeviceInfoEmpty checks if a device info is empty/not found
// Check multiple fields to ensure it's truly empty, as some fields like StorageType can be legitimately empty
func IsDeviceInfoEmpty(deviceInfo ybaclient.DeviceInfo) bool {
	return deviceInfo.NumVolumes == nil && deviceInfo.VolumeSize == nil &&
		deviceInfo.StorageType == nil
}
