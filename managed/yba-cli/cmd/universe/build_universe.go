/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"fmt"
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

func buildCommunicationPorts(cmd *cobra.Command) (
	*ybaclient.CommunicationPorts,
	error,
) {
	masterHTTPPort, err := cmd.Flags().GetInt("master-http-port")
	if err != nil {
		return nil, err
	}
	masterRPCPort, err := cmd.Flags().GetInt("master-rpc-port")
	if err != nil {
		return nil, err
	}
	nodeExporterPort, err := cmd.Flags().GetInt("node-exporter-port")
	if err != nil {
		return nil, err
	}
	redisServerHTTPPort, err := cmd.Flags().GetInt("redis-server-http-port")
	if err != nil {
		return nil, err
	}
	redisServerRPCPort, err := cmd.Flags().GetInt("redis-server-rpc-port")
	if err != nil {
		return nil, err
	}
	tserverHTTPPort, err := cmd.Flags().GetInt("tserver-http-port")
	if err != nil {
		return nil, err
	}
	tserverRPCPort, err := cmd.Flags().GetInt("tserver-rpc-port")
	if err != nil {
		return nil, err
	}
	yqlServerHTTPPort, err := cmd.Flags().GetInt("yql-server-http-port")
	if err != nil {
		return nil, err
	}
	yqlServerRPCPort, err := cmd.Flags().GetInt("yql-server-rpc-port")
	if err != nil {
		return nil, err
	}
	ysqlServerHTTPPort, err := cmd.Flags().GetInt("ysql-server-http-port")
	if err != nil {
		return nil, err
	}
	ysqlServerRPCPort, err := cmd.Flags().GetInt("ysql-server-rpc-port")
	if err != nil {
		return nil, err
	}

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
	}, nil
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
	providerType, err := cmd.Flags().GetString("provider-code")
	if err != nil {
		return nil, err
	}
	providerListRequest = providerListRequest.ProviderCode(providerType)
	providerName, err := cmd.Flags().GetString("provider-name")
	if err != nil {
		return nil, err
	}
	if providerName != "" {
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
	logrus.Info("Using provider: ",
		fmt.Sprintf("%s %s",
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

	addReadReplica, err := cmd.Flags().GetBool("add-read-replica")
	if err != nil {
		return nil, err
	}
	noOfClusters := 1
	if addReadReplica {
		noOfClusters = 2
	}

	dedicatedNodes, err := cmd.Flags().GetBool("dedicated-nodes")
	if err != nil {
		return nil, err
	}

	var k8sTserverMemSize, k8sMasterMemSize, k8sTserverCPUCoreCount, k8sMasterCPUCoreCount []float64

	if providerType == util.K8sProviderType {
		dedicatedNodes = true

		k8sTserverMemSize, err = cmd.Flags().GetFloat64Slice("k8s-tserver-mem-size")
		if err != nil {
			return nil, err
		}
		k8sTserverCPUCoreCount, err = cmd.Flags().GetFloat64Slice("k8s-tserver-cpu-core-count")
		if err != nil {
			return nil, err
		}

		k8sMasterMemSize, err = cmd.Flags().GetFloat64Slice("k8s-master-mem-size")
		if err != nil {
			return nil, err
		}
		k8sMasterCPUCoreCount, err = cmd.Flags().GetFloat64Slice("k8s-master-cpu-core-count")
		if err != nil {
			return nil, err
		}
	}

	var masterInstanceType string
	var masterDeviceInfo *ybaclient.DeviceInfo
	if dedicatedNodes {
		masterInstanceType, err = cmd.Flags().GetString("dedicated-master-instance-type")
		if err != nil {
			return nil, err
		}
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

	rfs, err := cmd.Flags().GetIntSlice("replication-factor")
	if err != nil {
		return nil, err
	}
	rfsLength := len(rfs)

	if rfsLength < noOfClusters {
		for i := 0; i < noOfClusters-rfsLength; i++ {
			rfs = append(rfs, 3) // since default is 3
		}
	}

	nodes, err := cmd.Flags().GetIntSlice("num-nodes")
	if err != nil {
		return nil, err
	}
	nodesLength := len(nodes)

	if nodesLength < noOfClusters {
		for i := 0; i < noOfClusters-nodesLength; i++ {
			nodes = append(nodes, 3) // since default is 3
		}
	}

	instanceTypes, err := cmd.Flags().GetStringArray("instance-type")
	if err != nil {
		return nil, err
	}
	// Set default values here
	instanceTypes, err = setDefaultInstanceTypes(
		instanceTypes, providerType, noOfClusters, onpremInstanceTypeDefault)
	if err != nil {
		return nil, err
	}

	regions := make([][]string, 0)
	regionsInput, err := cmd.Flags().GetStringArray("regions")
	if err != nil {
		return nil, err
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

	preferredRegionsInput, err := cmd.Flags().GetStringArray("preferred-region")
	if err != nil {
		return nil, err
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

	assignPublicIP, err := cmd.Flags().GetBool("assign-public-ip")
	if err != nil {
		return nil, err
	}

	assignStaticPublicIP, err := cmd.Flags().GetBool("assign-static-public-ip")
	if err != nil {
		return nil, err
	}

	enableYSQL, err := cmd.Flags().GetBool("enable-ysql")
	if err != nil {
		return nil, err
	}

	ysqlPassword, err := cmd.Flags().GetString("ysql-password")
	if err != nil {
		return nil, err
	}
	enableYSQLAuth := false
	if len(ysqlPassword) != 0 {
		enableYSQLAuth = true
	}

	enableYCQL, err := cmd.Flags().GetBool("enable-ycql")
	if err != nil {
		return nil, err
	}

	ycqlPassword, err := cmd.Flags().GetString("ycql-password")
	if err != nil {
		return nil, err
	}
	enableYCQLAuth := false
	if len(ycqlPassword) != 0 {
		enableYCQLAuth = true
	}

	enableYEDIS, err := cmd.Flags().GetBool("enable-yedis")
	if err != nil {
		return nil, err
	}

	enableCtoN, err := cmd.Flags().GetBool("enable-client-to-node-encrypt")
	if err != nil {
		return nil, err
	}

	enableNtoN, err := cmd.Flags().GetBool("enable-node-to-node-encrypt")
	if err != nil {
		return nil, err
	}

	ybSoftwareVersion, err := cmd.Flags().GetString("yb-db-version")
	if err != nil {
		return nil, err
	}
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

	useSystemD, err := cmd.Flags().GetBool("use-systemd")
	if err != nil {
		return nil, err
	}

	accessKeyCode, err := cmd.Flags().GetString("access-key-code")
	if err != nil {
		return nil, err
	}
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

	awsARNString, err := cmd.Flags().GetString("aws-arn-string")
	if err != nil {
		return nil, err
	}
	if providerType != util.AWSProviderType && len(awsARNString) > 0 {
		logrus.Debug("aws-arn-string can only be set for AWS universes, ignoring value\n")
		awsARNString = ""
	}

	enableIPV6, err := cmd.Flags().GetBool("enable-ipv6")
	if err != nil {
		return nil, err
	}
	if providerType != util.K8sProviderType && enableIPV6 {
		logrus.Debug("enable-ipv6 can only be set for Kubernetes universes, ignoring value\n")
		enableIPV6 = false
	}

	k8sUniverseOverridesFilePath, err := cmd.Flags().GetString(
		"kubernetes-universe-overrides-file-path")
	if err != nil {
		return nil, err
	}
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

	k8sAZOverridesFilePaths, err := cmd.Flags().GetStringArray(
		"kubernetes-az-overrides-file-path")
	if err != nil {
		return nil, err
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

	userTagsMap, err := cmd.Flags().GetStringToString("user-tags")
	if err != nil {
		return nil, err
	}

	masterGFlagsString, err := cmd.Flags().GetString("master-gflags")
	if err != nil {
		return nil, err
	}
	masterGFlags := upgrade.FetchMasterGFlags(masterGFlagsString)

	tserverGFlagsStringList, err := cmd.Flags().GetStringArray("tserver-gflags")
	if err != nil {
		return nil, err
	}
	tserverGFlagsList := upgrade.FetchTServerGFlags(tserverGFlagsStringList, noOfClusters)

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

				InstanceType: util.GetStringPointer(instanceTypes[i]),
				DeviceInfo:   deviceInfo[i],

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

	diskIops, err := cmd.Flags().GetIntSlice("disk-iops")
	if err != nil {
		return nil, err
	}
	diskIopsLen := len(diskIops)

	numVolumes, err := cmd.Flags().GetIntSlice("num-volumes")
	if err != nil {
		return nil, err
	}
	numVolumeLen := len(numVolumes)

	volumeSize, err := cmd.Flags().GetIntSlice("volume-size")
	if err != nil {
		return nil, err
	}
	volumeSizeLen := len(volumeSize)

	storageType, err := cmd.Flags().GetStringArray("storage-type")
	if err != nil {
		return nil, err
	}
	storageTypeLen := len(storageType)

	storageClass, err := cmd.Flags().GetStringArray("storage-class")
	if err != nil {
		return nil, err
	}
	storageClassLen := len(storageClass)

	throughput, err := cmd.Flags().GetIntSlice("throughput")
	if err != nil {
		return nil, err
	}
	throughputLen := len(throughput)

	mountPoints, err := cmd.Flags().GetStringArray("mount-points")
	if err != nil {
		return nil, err
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

	diskIops, err := cmd.Flags().GetInt("dedicated-master-disk-iops")
	if err != nil {
		return nil, err
	}

	numVolumes, err := cmd.Flags().GetInt("dedicated-master-num-volumes")
	if err != nil {
		return nil, err
	}

	volumeSize, err := cmd.Flags().GetInt("dedicated-master-volume-size")
	if err != nil {
		return nil, err
	}

	storageType, err := cmd.Flags().GetString("dedicated-master-storage-type")
	if err != nil {
		return nil, err
	}

	storageClass, err := cmd.Flags().GetString("dedicated-master-storage-class")
	if err != nil {
		return nil, err
	}

	throughput, err := cmd.Flags().GetInt("dedicated-master-throughput")
	if err != nil {
		return nil, err
	}

	mountPoints, err := cmd.Flags().GetString("dedicated-master-mount-points")
	if err != nil {
		return nil, err
	}

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
