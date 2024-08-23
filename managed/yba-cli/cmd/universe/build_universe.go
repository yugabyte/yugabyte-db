/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"fmt"
	"reflect"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/releases"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/upgrade"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var checkInterfaceType []interface{}

func buildCommunicationPorts(cmd *cobra.Command) *ybaclient.CommunicationPorts {
	masterHTTPPort := v1.GetInt("master-http-port")
	masterRPCPort := v1.GetInt("master-rpc-port")
	nodeExporterPort := v1.GetInt("node-exporter-port")
	redisServerHTTPPort := v1.GetInt("redis-server-http-port")
	redisServerRPCPort := v1.GetInt("redis-server-rpc-port")
	tserverHTTPPort := v1.GetInt("tserver-http-port")
	tserverRPCPort := v1.GetInt("tserver-rpc-port")
	yqlServerHTTPPort := v1.GetInt("yql-server-http-port")
	yqlServerRPCPort := v1.GetInt("yql-server-rpc-port")
	ysqlServerHTTPPort := v1.GetInt("ysql-server-http-port")
	ysqlServerRPCPort := v1.GetInt("ysql-server-rpc-port")

	return &ybaclient.CommunicationPorts{
		MasterHttpPort:      util.GetInt32Pointer(int32(masterHTTPPort)),
		MasterRpcPort:       util.GetInt32Pointer(int32(masterRPCPort)),
		NodeExporterPort:    util.GetInt32Pointer(int32(nodeExporterPort)),
		RedisServerHttpPort: util.GetInt32Pointer(int32(redisServerHTTPPort)),
		RedisServerRpcPort:  util.GetInt32Pointer(int32(redisServerRPCPort)),
		TserverHttpPort:     util.GetInt32Pointer(int32(tserverHTTPPort)),
		TserverRpcPort:      util.GetInt32Pointer(int32(tserverRPCPort)),
		YqlServerHttpPort:   util.GetInt32Pointer(int32(yqlServerHTTPPort)),
		YqlServerRpcPort:    util.GetInt32Pointer(int32(yqlServerRPCPort)),
		YsqlServerHttpPort:  util.GetInt32Pointer(int32(ysqlServerHTTPPort)),
		YsqlServerRpcPort:   util.GetInt32Pointer(int32(ysqlServerRPCPort)),
	}
}

func buildClusters(
	cmd *cobra.Command,
	authAPI *ybaAuthClient.AuthAPIClient,
	universeName string,
) (
	[]ybaclient.Cluster,
	error,
) {
	var res []ybaclient.Cluster

	providerListRequest := authAPI.GetListOfProviders()
	providerType := v1.GetString("provider-code")
	if len(strings.TrimSpace(providerType)) == 0 {
		logrus.Fatalln(formatter.Colorize("No provider code found\n", formatter.RedColor))
	}
	providerListRequest = providerListRequest.ProviderCode(providerType)
	providerName := v1.GetString("provider-name")
	if strings.TrimSpace(providerName) != "" {
		providerListRequest = providerListRequest.Name(providerName)
	}
	providerListResponse, response, err := providerListRequest.Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"Universe", "Create - Fetch Providers")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	if len(providerListResponse) < 1 {
		return nil, fmt.Errorf("no provider found")
	}
	providerUsed := providerListResponse[0]
	providerUUID := providerUsed.GetUuid()
	if len(providerName) == 0 {
		providerName = providerUsed.GetName()
	}
	logrus.Info(fmt.Sprintf("Using %s provider: %s %s",
		providerType,
		providerName,
		formatter.Colorize(providerUUID, formatter.GreenColor)), "\n")

	var onpremInstanceTypeDefault ybaclient.InstanceTypeResp
	if providerType == util.OnpremProviderType {
		onpremInstanceTypes, response, err := authAPI.ListOfInstanceType(providerUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe",
				"Create - Fetch Instance Types")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(onpremInstanceTypes) > 0 {
			onpremInstanceTypeDefault = onpremInstanceTypes[0]
		}
	}

	cpuArch := v1.GetString("cpu-architecture")
	if len(strings.TrimSpace(cpuArch)) == 0 {
		cpuArch = util.X86_64
	}

	imageBundlesInProvider := providerUsed.GetImageBundles()
	if len(imageBundlesInProvider) == 0 {
		return nil, fmt.Errorf("no image bundles found for provider %s", providerName)
	}

	addReadReplica := v1.GetBool("add-read-replica")
	noOfClusters := 1
	if addReadReplica {
		noOfClusters = 2
	}

	linuxVersionsInterface := v1.Get("linux-version")
	var linuxVersionsInput []string
	if reflect.TypeOf(linuxVersionsInterface) == reflect.TypeOf(checkInterfaceType) {
		linuxVersionsInput = *util.StringSlice(linuxVersionsInterface.([]interface{}))
	} else {
		linuxVersionsInput = linuxVersionsInterface.([]string)
	}

	var imageBundleUUIDs []string
	for _, l := range linuxVersionsInput {
		for _, ib := range imageBundlesInProvider {
			ibDetails := ib.GetDetails()
			if strings.Compare(ib.GetName(), l) == 0 &&
				strings.Compare(ibDetails.GetArch(), cpuArch) == 0 {
				imageBundleUUIDs = append(imageBundleUUIDs, ib.GetUuid())
			}
		}
	}

	if len(imageBundleUUIDs) != len(linuxVersionsInput) {
		fmt.Errorf("the provided linux version name cannot be found")
	}

	imageBundleLen := len(imageBundleUUIDs)
	for i := 0; i < noOfClusters-imageBundleLen; i++ {
		for _, ib := range imageBundlesInProvider {
			ibDetails := ib.GetDetails()
			if strings.Compare(ibDetails.GetArch(), cpuArch) == 0 &&
				ib.GetUseAsDefault() {
				imageBundleUUIDs = append(imageBundleUUIDs, ib.GetUuid())
			}
		}
	}

	logrus.Info("Using image bundles: ", imageBundleUUIDs, "\n")

	dedicatedNodes := v1.GetBool("dedicated-nodes")

	var k8sTserverMemSize, k8sMasterMemSize, k8sTserverCPUCoreCount, k8sMasterCPUCoreCount []float64

	if providerType == util.K8sProviderType {
		dedicatedNodes = true

		k8sTserverMemSizeInterface := v1.Get("k8s-tserver-mem-size")
		if reflect.TypeOf(k8sTserverMemSizeInterface) == reflect.TypeOf(checkInterfaceType) {
			k8sTserverMemSize = *util.Float64Slice(k8sTserverMemSizeInterface.([]interface{}))
		} else {
			k8sTserverMemSize = k8sTserverMemSizeInterface.([]float64)
		}

		k8sTserverCPUCoreCountInterface := v1.Get("k8s-tserver-cpu-core-count")
		if reflect.TypeOf(k8sTserverCPUCoreCountInterface) == reflect.TypeOf(checkInterfaceType) {
			k8sTserverCPUCoreCount = *util.Float64Slice(k8sTserverCPUCoreCountInterface.([]interface{}))
		} else {
			k8sTserverCPUCoreCount = k8sTserverCPUCoreCountInterface.([]float64)
		}

		k8sMasterMemSizeInterface := v1.Get("k8s-master-mem-size")
		if reflect.TypeOf(k8sMasterMemSizeInterface) == reflect.TypeOf(checkInterfaceType) {
			k8sMasterMemSize = *util.Float64Slice(k8sMasterMemSizeInterface.([]interface{}))
		} else {
			k8sMasterMemSize = k8sMasterMemSizeInterface.([]float64)
		}

		k8sMasterCPUCoreCountInterface := v1.Get("k8s-master-cpu-core-count")
		if reflect.TypeOf(k8sMasterCPUCoreCountInterface) == reflect.TypeOf(checkInterfaceType) {
			k8sMasterCPUCoreCount = *util.Float64Slice(k8sMasterCPUCoreCountInterface.([]interface{}))
		} else {
			k8sMasterCPUCoreCount = k8sMasterCPUCoreCountInterface.([]float64)
		}
	}

	var masterInstanceType string
	var masterDeviceInfo *ybaclient.DeviceInfo
	if dedicatedNodes {
		masterInstanceType = v1.GetString("dedicated-master-instance-type")
		// Set default values here
		masterInstanceTypeList, err := setDefaultInstanceTypes(
			[]string{masterInstanceType}, providerType, 1, onpremInstanceTypeDefault)
		if err != nil {
			return nil, err
		}
		if len(masterInstanceTypeList) > 0 {
			masterInstanceType = masterInstanceTypeList[0]
		}
		masterDeviceInfo, err = buildMasterDeviceInfo(cmd, providerType, masterInstanceType, onpremInstanceTypeDefault)
		if err != nil {
			return nil, err
		}
	}

	rfs := v1.GetIntSlice("replication-factor")
	rfsLength := len(rfs)

	if rfsLength < noOfClusters {
		for i := 0; i < noOfClusters-rfsLength; i++ {
			rfs = append(rfs, 3) // since default is 3
		}
	}

	nodes := v1.GetIntSlice("num-nodes")
	nodesLength := len(nodes)

	if nodesLength < noOfClusters {
		for i := 0; i < noOfClusters-nodesLength; i++ {
			nodes = append(nodes, 3) // since default is 3
		}
	}

	instanceTypesInterface := v1.Get("instance-type")
	var instanceTypes []string
	if reflect.TypeOf(instanceTypesInterface) == reflect.TypeOf(checkInterfaceType) {
		instanceTypes = *util.StringSlice(instanceTypesInterface.([]interface{}))
	} else {
		instanceTypes = instanceTypesInterface.([]string)
	}

	// Set default values here
	instanceTypes, err = setDefaultInstanceTypes(
		instanceTypes, providerType, noOfClusters, onpremInstanceTypeDefault)
	if err != nil {
		return nil, err
	}

	regions := make([][]string, 0)

	regionsInputInterface := v1.Get("regions")
	var regionsInput []string
	if reflect.TypeOf(regionsInputInterface) == reflect.TypeOf(checkInterfaceType) {
		regionsInput = *util.StringSlice(regionsInputInterface.([]interface{}))
	} else {
		regionsInput = regionsInputInterface.([]string)
	}

	regionsInProvider := providerUsed.GetRegions()
	if len(regionsInProvider) == 0 {
		return nil, fmt.Errorf("no regions found for provider %s", providerName)
	}

	for _, regionString := range regionsInput {
		regionCodeList := make([]string, 0)
		regionCodeList = append(regionCodeList, strings.Split(regionString, ",")...)

		regionUUIDList := make([]string, 0)
		for _, r := range regionCodeList {
			if len(regionsInProvider) > 0 {
				for _, rInProvider := range regionsInProvider {
					if strings.Compare(r, rInProvider.GetCode()) == 0 {
						regionUUIDList = append(regionUUIDList, rInProvider.GetUuid())
					}
				}
			}
		}
		if len(regionUUIDList) != len(regionCodeList) {
			return nil, fmt.Errorf("the provided region name cannot be found")
		}
		regions = append(regions, regionUUIDList)
	}

	regionsLen := len(regions)

	for i := 0; i < noOfClusters-regionsLen; i++ {
		regionUUIDList := make([]string, 0)
		for _, rInProvider := range regionsInProvider {
			regionUUIDList = append(regionUUIDList, rInProvider.GetUuid())
		}
		regions = append(regions, regionUUIDList)
	}

	logrus.Info("Using regions: ", regions, "\n")

	preferredRegionsInputInterface := v1.Get("preferred-region")
	var preferredRegionsInput []string
	if reflect.TypeOf(preferredRegionsInputInterface) == reflect.TypeOf(checkInterfaceType) {
		preferredRegionsInput = *util.StringSlice(preferredRegionsInputInterface.([]interface{}))
	} else {
		preferredRegionsInput = preferredRegionsInputInterface.([]string)
	}

	preferredRegions := make([]string, 0)
	for _, r := range preferredRegionsInput {
		if len(regionsInProvider) > 0 {
			for _, rInProvider := range regionsInProvider {
				if strings.Compare(r, rInProvider.GetCode()) == 0 {
					preferredRegions = append(preferredRegions, rInProvider.GetUuid())
				}
			}
		}
	}
	preferredRegionsLen := len(preferredRegions)
	for i := 0; i < noOfClusters-preferredRegionsLen; i++ {
		preferredRegions = append(preferredRegions, "")
	}

	logrus.Info("Using preferred regions: ", preferredRegions, "\n")

	deviceInfo, err := buildDeviceInfo(cmd, providerType, noOfClusters, instanceTypes, onpremInstanceTypeDefault)
	if err != nil {
		return nil, err
	}

	assignPublicIP := v1.GetBool("assign-public-ip")

	assignStaticPublicIP := v1.GetBool("assign-static-public-ip")

	enableYSQL := v1.GetBool("enable-ysql")

	ysqlPassword := v1.GetString("ysql-password")
	enableYSQLAuth := false
	if len(ysqlPassword) != 0 {
		enableYSQLAuth = true
	}

	enableYCQL := v1.GetBool("enable-ycql")

	ycqlPassword := v1.GetString("ycql-password")

	enableYCQLAuth := false
	if len(ycqlPassword) != 0 {
		enableYCQLAuth = true
	}

	enableYEDIS := v1.GetBool("enable-yedis")

	enableCtoN := v1.GetBool("enable-client-to-node-encrypt")

	enableNtoN := v1.GetBool("enable-node-to-node-encrypt")

	ybSoftwareVersion := v1.GetString("yb-db-version")
	if len(ybSoftwareVersion) == 0 {
		// Fetch the latest release
		releasesListRequest := authAPI.GetListOfReleases(true)

		r, response, err := releasesListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Universe", "Create - Fetch Releases")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		sortedReleases := releases.SortReleasesWithMetadata(r)
		if len(sortedReleases) > 0 {
			ybSoftwareVersion = sortedReleases[0]["version"].(string)
		} else {
			return nil, fmt.Errorf("no YugabyteDB version found")
		}
	}
	logrus.Info("Using YugabyteDB Version: ",
		formatter.Colorize(ybSoftwareVersion, formatter.GreenColor), "\n")

	useSystemD := v1.GetBool("use-systemd")

	accessKeyCode := v1.GetString("access-key-code")

	if len(accessKeyCode) == 0 && providerType != util.K8sProviderType {
		r, response, err := authAPI.List(providerUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Universe", "Create - Fetch Access Keys")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(r) < 1 {
			return nil, fmt.Errorf("no Access keys found")
		}
		idKey := r[0].GetIdKey()
		accessKeyCode = idKey.GetKeyCode()
		logrus.Info("Using access key: ",
			formatter.Colorize(accessKeyCode, formatter.GreenColor), "\n")
	} else if len(accessKeyCode) > 0 {
		logrus.Info("Using access key: ",
			formatter.Colorize(accessKeyCode, formatter.GreenColor), "\n")
	}

	awsARNString := v1.GetString("aws-arn-string")

	if providerType != util.AWSProviderType && len(awsARNString) > 0 {
		logrus.Debug("aws-arn-string can only be set for AWS universes, ignoring value\n")
		awsARNString = ""
	}

	enableIPV6 := v1.GetBool("enable-ipv6")

	if providerType != util.K8sProviderType && enableIPV6 {
		logrus.Debug("enable-ipv6 can only be set for Kubernetes universes, ignoring value\n")
		enableIPV6 = false
	}

	k8sUniverseOverridesFilePath := v1.GetString(
		"kubernetes-universe-overrides-file-path")

	if providerType != util.K8sProviderType && len(k8sUniverseOverridesFilePath) > 0 {
		logrus.Debug(
			"kubernetest-universe-overrides-file-path can only be set for" +
				" Kubernetes universes, ignoring value\n")
		k8sUniverseOverridesFilePath = ""
	}
	var k8sUniverseOverrides string
	if len(k8sUniverseOverridesFilePath) > 0 {
		logrus.Debug("Reading Helm Universe Overrides")
		k8sUniverseOverrides = util.YAMLtoString(k8sUniverseOverridesFilePath)
	}

	k8sAZOverridesFilePathsInterface := v1.Get("kubernetes-az-overrides-file-path")
	var k8sAZOverridesFilePaths []string
	if reflect.TypeOf(k8sAZOverridesFilePathsInterface) == reflect.TypeOf(checkInterfaceType) {
		k8sAZOverridesFilePaths = *util.StringSlice(k8sAZOverridesFilePathsInterface.([]interface{}))
	} else {
		k8sAZOverridesFilePaths = k8sAZOverridesFilePathsInterface.([]string)
	}

	if providerType != util.K8sProviderType && len(k8sAZOverridesFilePaths) > 0 {
		logrus.Debug(
			"kubernetest-az-overrides-file-path can only be set for" +
				" Kubernetes universes, ignoring value\n")
		k8sAZOverridesFilePaths = make([]string, 0)
	}

	k8sAZOverridesMap := make(map[string]string, 0)
	for _, k := range k8sAZOverridesFilePaths {
		var k8sAZOverrides string
		if len(k) > 0 {
			logrus.Debug("Reading Helm AZ Overrides")
			k8sAZOverrides = util.YAMLtoString(k)
		}
		regionIndex := strings.Index(k8sAZOverrides, "\n")

		region := strings.TrimSpace(strings.ReplaceAll(k8sAZOverrides[:regionIndex], ":", ""))
		regionOverride := k8sAZOverrides[regionIndex+1:]

		if len(region) > 0 && len(regionOverride) > 0 {
			k8sAZOverridesMap[region] = regionOverride
		}
	}

	userTagsMap := v1.GetStringMapString("user-tags")

	masterGFlagsString := v1.GetString("master-gflags")
	var masterGFlags map[string]string
	if len(strings.TrimSpace(masterGFlagsString)) > 0 {
		if strings.HasPrefix(strings.TrimSpace(masterGFlagsString), "{") {
			masterGFlags = upgrade.ProcessMasterGflagsJSONString(masterGFlagsString)
		} else {
			// Assume YAML format
			masterGFlags = upgrade.ProcessMasterGflagsYAMLString(masterGFlagsString)
		}
	} else {
		masterGflagsMap := v1.GetStringMapString("master-gflags")
		if len(masterGflagsMap) != 0 {
			logrus.Debugf("Master GFlags from config file: %v", masterGflagsMap)
			masterGFlags = masterGflagsMap
		}
	}

	var tserverGflagsMapOfMaps map[string]map[string]string
	if strings.TrimSpace(tserverGflagsString) != "" {
		if err := upgrade.ProcessTServerGFlagsFromString(tserverGflagsString, &tserverGflagsMapOfMaps); err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	} else {
		tserverGflags := v1.GetStringMap("tserver-gflags")
		if len(tserverGflags) != 0 {
			tserverGflagsMapOfMaps = upgrade.ProcessTServerGFlagsFromConfig(tserverGflags)
			logrus.Debugf("TServer GFlags from config file: %v", tserverGflagsMapOfMaps)
		}
	}

	tserverGFlagsList := make([]map[string]string, 2)
	for k, v := range tserverGflagsMapOfMaps {
		if strings.Compare(strings.ToUpper(k), util.PrimaryClusterType) == 0 {
			tserverGFlagsList[0] = v
		} else if strings.Compare(
			strings.ToUpper(k), util.ReadReplicaClusterType) == 0 {
			if len(v) != 0 {
				tserverGFlagsList[1] = v
			} else {
				tserverGFlagsList[1] = tserverGFlagsList[0]
			}
		} else {
			logrus.Debug("Not a valid cluster type, ignoring value\n")
		}
	}

	for i := 0; i < noOfClusters; i++ {
		var clusterType string
		if i == 0 {
			clusterType = util.PrimaryClusterType
		} else {
			clusterType = util.ReadReplicaClusterType
		}
		c := ybaclient.Cluster{
			ClusterType: clusterType,
			Index:       util.GetInt32Pointer(int32(i)),
			UserIntent: ybaclient.UserIntent{
				UniverseName:   util.GetStringPointer(universeName),
				ProviderType:   util.GetStringPointer(providerType),
				Provider:       util.GetStringPointer(providerUUID),
				DedicatedNodes: util.GetBoolPointer(dedicatedNodes),

				InstanceType:    util.GetStringPointer(instanceTypes[i]),
				ImageBundleUUID: util.GetStringPointer(imageBundleUUIDs[i]),
				DeviceInfo:      deviceInfo[i],

				MasterInstanceType: util.GetStringPointer(masterInstanceType),
				MasterDeviceInfo:   masterDeviceInfo,

				AssignPublicIP:       util.GetBoolPointer(assignPublicIP),
				AssignStaticPublicIP: util.GetBoolPointer(assignStaticPublicIP),
				EnableYSQL:           util.GetBoolPointer(enableYSQL),
				YsqlPassword:         util.GetStringPointer(ysqlPassword),
				EnableYSQLAuth:       util.GetBoolPointer(enableYSQLAuth),
				EnableYCQL:           util.GetBoolPointer(enableYCQL),
				YcqlPassword:         util.GetStringPointer(ycqlPassword),
				EnableYCQLAuth:       util.GetBoolPointer(enableYCQLAuth),
				EnableYEDIS:          util.GetBoolPointer(enableYEDIS),

				EnableClientToNodeEncrypt: util.GetBoolPointer(enableCtoN),
				EnableNodeToNodeEncrypt:   util.GetBoolPointer(enableNtoN),

				UseSystemd:        util.GetBoolPointer(useSystemD),
				YbSoftwareVersion: util.GetStringPointer(ybSoftwareVersion),
				AccessKeyCode:     util.GetStringPointer(accessKeyCode),
				EnableIPV6:        util.GetBoolPointer(enableIPV6),
				InstanceTags:      util.StringtoStringMap(userTagsMap),

				ReplicationFactor: util.GetInt32Pointer(int32(rfs[i])),
				NumNodes:          util.GetInt32Pointer(int32(nodes[i])),
				RegionList:        util.StringSliceFromString(regions[i]),
				PreferredRegion:   util.GetStringPointer(preferredRegions[i]),
				AwsArnString:      util.GetStringPointer(awsARNString),

				MasterGFlags:      util.StringtoStringMap(masterGFlags),
				TserverGFlags:     util.StringtoStringMap(tserverGFlagsList[i]),
				UniverseOverrides: util.GetStringPointer(k8sUniverseOverrides),
				AzOverrides:       util.StringtoStringMap(k8sAZOverridesMap),
			},
		}
		if providerType == util.K8sProviderType {
			k8sTserverMemSizeLen := len(k8sTserverMemSize)
			k8sMasterMemSizeLen := len(k8sMasterMemSize)
			k8sTserverCPUCoreCountLen := len(k8sTserverCPUCoreCount)
			k8sMasterCPUCoreCountLen := len(k8sMasterCPUCoreCount)
			if i == k8sTserverMemSizeLen {
				k8sTserverMemSize = append(k8sTserverMemSize, 4)
				k8sTserverMemSizeLen = k8sTserverMemSizeLen + 1
			}
			if i == k8sMasterMemSizeLen {
				k8sMasterMemSize = append(k8sMasterMemSize, 4)
				k8sMasterMemSizeLen = k8sMasterMemSizeLen + 1
			}
			if i == k8sTserverCPUCoreCountLen {
				k8sTserverCPUCoreCount = append(k8sTserverCPUCoreCount, 2)
				k8sTserverCPUCoreCountLen = k8sTserverCPUCoreCountLen + 1
			}
			if i == k8sMasterCPUCoreCountLen {
				k8sMasterCPUCoreCount = append(k8sTserverCPUCoreCount, 2)
				k8sMasterCPUCoreCountLen = k8sMasterCPUCoreCountLen + 1
			}
			userIntent := c.GetUserIntent()
			userIntent.SetTserverK8SNodeResourceSpec(ybaclient.K8SNodeResourceSpec{
				MemoryGib:    k8sTserverMemSize[i],
				CpuCoreCount: k8sTserverCPUCoreCount[i],
			})
			userIntent.SetMasterK8SNodeResourceSpec(ybaclient.K8SNodeResourceSpec{
				MemoryGib:    k8sMasterMemSize[i],
				CpuCoreCount: k8sMasterCPUCoreCount[i],
			})
			c.SetUserIntent(userIntent)
		}
		res = append(res, c)
	}

	return res, nil
}

func buildDeviceInfo(
	cmd *cobra.Command,
	providerType string,
	noOfClusters int,
	instanceTypes []string,
	onpremInstanceTypeDefault ybaclient.InstanceTypeResp) (
	deviceInfos []*ybaclient.DeviceInfo,
	err error,
) {
	onpremInstanceTypeDetailsDefault := onpremInstanceTypeDefault.GetInstanceTypeDetails()
	onpremVolumeDefaultList := onpremInstanceTypeDetailsDefault.GetVolumeDetailsList()
	var onpremVolumeDefault ybaclient.VolumeDetails
	if len(onpremVolumeDefaultList) > 0 {
		onpremVolumeDefault = onpremVolumeDefaultList[0]
	}

	diskIops := v1.GetIntSlice("disk-iops")
	diskIopsLen := len(diskIops)

	numVolumes := v1.GetIntSlice("num-volumes")
	numVolumeLen := len(numVolumes)

	volumeSize := v1.GetIntSlice("volume-size")
	volumeSizeLen := len(volumeSize)

	storageTypeInterface := v1.Get("storage-type")
	var storageType []string
	if reflect.TypeOf(storageTypeInterface) == reflect.TypeOf(checkInterfaceType) {
		storageType = *util.StringSlice(storageTypeInterface.([]interface{}))
	} else {
		storageType = storageTypeInterface.([]string)
	}
	storageTypeLen := len(storageType)

	storageClassInterface := v1.Get("storage-class")
	var storageClass []string
	if reflect.TypeOf(storageClassInterface) == reflect.TypeOf(checkInterfaceType) {
		storageClass = *util.StringSlice(storageClassInterface.([]interface{}))
	} else {
		storageClass = storageClassInterface.([]string)
	}
	storageClassLen := len(storageClass)

	throughput := v1.GetIntSlice("throughput")
	throughputLen := len(throughput)

	mountPointsInterface := v1.Get("mount-points")
	var mountPoints []string
	if reflect.TypeOf(mountPointsInterface) == reflect.TypeOf(checkInterfaceType) {
		mountPoints = *util.StringSlice(mountPointsInterface.([]interface{}))
	} else {
		mountPoints = mountPointsInterface.([]string)
	}
	mountPointsLen := len(mountPoints)

	for i := 0; i < noOfClusters; i++ {
		if providerType == util.AWSProviderType {
			if i == diskIopsLen { // avoid index not accessible error
				diskIops = append(diskIops, 3000) // default is 3000
				diskIopsLen = diskIopsLen + 1
			}
			if i == throughputLen {
				throughput = append(throughput, 125)
				throughputLen = throughputLen + 1
			}
		} else {
			if i == diskIopsLen {
				diskIops = append(diskIops, 0) // no value accepted for other providers
				diskIopsLen = diskIopsLen + 1
			} else {
				diskIops[i] = 0
			}
			if i == throughputLen {
				throughput = append(throughput, 0)
				throughputLen = throughputLen + 1
			} else {
				throughput[i] = 0
			}
		}
		if providerType == util.OnpremProviderType {
			if i == mountPointsLen {
				mountPoints = append(mountPoints, onpremVolumeDefault.GetMountPath())
				mountPointsLen = mountPointsLen + 1
			}
		} else {
			if i == mountPointsLen {
				mountPoints = append(mountPoints, "")
				mountPointsLen = mountPointsLen + 1
			} else {
				mountPoints[i] = ""
			}
		}
		if providerType == util.K8sProviderType {
			if i == storageClassLen {
				storageClass = append(storageClass, "standard")
				storageClassLen = storageClassLen + 1
			}
		} else {
			if i == storageClassLen {
				storageClass = append(storageClass, "")
				storageClassLen = storageClassLen + 1
			} else {
				storageClass[i] = ""
			}
		}
		if i == numVolumeLen {
			numVolumes = append(numVolumes, 1)
			numVolumeLen = numVolumeLen + 1
		}
		if i == volumeSizeLen {
			if providerType != util.OnpremProviderType {
				volumeSize = append(volumeSize, 100)
			} else {
				volumeSize = append(volumeSize, int(onpremVolumeDefault.GetVolumeSizeGB()))
			}
			volumeSizeLen = volumeSizeLen + 1
		}
		if i == storageTypeLen {
			storageTypeDefault := setDefaultStorageTypes(providerType, onpremVolumeDefault)
			if providerType == util.AWSProviderType && util.AwsInstanceTypesWithEphemeralStorageOnly(instanceTypes[i]) {
				storageTypeDefault = ""
			}
			storageType = append(storageType, storageTypeDefault)
			storageTypeLen = storageTypeLen + 1
		}
		deviceInfo := &ybaclient.DeviceInfo{
			DiskIops:     util.GetInt32Pointer(int32(diskIops[i])),
			MountPoints:  util.GetStringPointer(mountPoints[i]),
			StorageClass: util.GetStringPointer(storageClass[i]),
			Throughput:   util.GetInt32Pointer(int32(throughput[i])),
			NumVolumes:   util.GetInt32Pointer(int32(numVolumes[i])),
			VolumeSize:   util.GetInt32Pointer(int32(volumeSize[i])),
			StorageType:  util.GetStringPointer(storageType[i]),
		}
		deviceInfos = append(deviceInfos, deviceInfo)
	}
	return deviceInfos, nil
}

func buildMasterDeviceInfo(
	cmd *cobra.Command,
	providerType string,
	instanceType string,
	onpremInstanceTypeDefault ybaclient.InstanceTypeResp) (
	deviceInfos *ybaclient.DeviceInfo,
	err error,
) {
	onpremInstanceTypeDetailsDefault := onpremInstanceTypeDefault.GetInstanceTypeDetails()
	onpremVolumeDefaultList := onpremInstanceTypeDetailsDefault.GetVolumeDetailsList()
	var onpremVolumeDefault ybaclient.VolumeDetails
	if len(onpremVolumeDefaultList) > 0 {
		onpremVolumeDefault = onpremVolumeDefaultList[0]
	}

	diskIops := v1.GetInt("dedicated-master-disk-iops")
	numVolumes := v1.GetInt("dedicated-master-num-volumes")
	volumeSize := v1.GetInt("dedicated-master-volume-size")
	storageType := v1.GetString("dedicated-master-storage-type")
	storageClass := v1.GetString("dedicated-master-storage-class")
	throughput := v1.GetInt("dedicated-master-throughput")
	mountPoints := v1.GetString("dedicated-master-mount-points")

	if providerType != util.AWSProviderType {
		diskIops = 0
		throughput = 0
	}
	if providerType == util.OnpremProviderType {
		if len(mountPoints) == 0 {
			mountPoints = onpremVolumeDefault.GetMountPath()
		}
	} else {
		mountPoints = ""
	}

	if providerType == util.OnpremProviderType && volumeSize == 100 {
		volumeSize = int(onpremVolumeDefault.GetVolumeSizeGB())
	}

	if len(storageType) == 0 {
		storageType = setDefaultStorageTypes(providerType, onpremVolumeDefault)
		if providerType == util.AWSProviderType && util.AwsInstanceTypesWithEphemeralStorageOnly(instanceType) {
			storageType = ""
		}
	}

	if len(storageClass) == 0 {
		storageClass = "standard"

	}
	return &ybaclient.DeviceInfo{
		DiskIops:     util.GetInt32Pointer(int32(diskIops)),
		MountPoints:  util.GetStringPointer(mountPoints),
		StorageClass: util.GetStringPointer(storageClass),
		Throughput:   util.GetInt32Pointer(int32(throughput)),
		NumVolumes:   util.GetInt32Pointer(int32(numVolumes)),
		VolumeSize:   util.GetInt32Pointer(int32(volumeSize)),
		StorageType:  util.GetStringPointer(storageType),
	}, nil
}

func setDefaultInstanceTypes(
	instanceTypes []string,
	providerType string,
	noOfClusters int,
	onpremInstanceTypeDefault ybaclient.InstanceTypeResp,
) ([]string, error) {
	instanceTypesLen := len(instanceTypes)
	if instanceTypesLen != noOfClusters {
		for i := 0; i < noOfClusters-instanceTypesLen; i++ {
			var instanceTypeDefault string
			switch providerType {
			case util.AWSProviderType:
				instanceTypeDefault = "c5.large"
			case util.AzureProviderType:
				instanceTypeDefault = "Standard_DS2_v2"
			case util.GCPProviderType:
				instanceTypeDefault = "n1-standard-1"
			case util.OnpremProviderType:
				instanceTypeDefault = onpremInstanceTypeDefault.GetInstanceTypeCode()
			case util.K8sProviderType:
				instanceTypeDefault = ""
			}
			instanceTypes = append(instanceTypes, instanceTypeDefault)
		}
	}
	return instanceTypes, nil
}

func setDefaultStorageTypes(
	providerType string,
	onpremVolumeDefault ybaclient.VolumeDetails,
) (
	storageType string,
) {
	switch providerType {
	case util.AWSProviderType:
		storageType = "GP3"
	case util.AzureProviderType:
		storageType = "Premium_LRS"
	case util.GCPProviderType:
		storageType = "Persistent"
	case util.OnpremProviderType:
		storageType = ""
	}
	return storageType
}
