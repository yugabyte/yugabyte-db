/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universeutil

import (
	"encoding/json"
	"fmt"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"gopkg.in/yaml.v2"

	"github.com/spf13/pflag"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
)

// PopulateDescribeUniverseCLIOutput fetches required information from the universe
func PopulateDescribeUniverseCLIOutput(
	flagsInCreateUniverse *pflag.FlagSet,
	u ybaclient.UniverseResp,
	outputFormat string,
) {

	details := u.GetUniverseDetails()
	resources := u.GetResources()
	primaryCluster := FindClusterByType(details.GetClusters(), util.PrimaryClusterType)
	rrCluster := FindClusterByType(details.GetClusters(), util.ReadReplicaClusterType)
	if IsClusterEmpty(primaryCluster) {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf(
					"No primary cluster found in universe %s (%s)\n",
					u.GetName(),
					u.GetUniverseUUID(),
				),
				formatter.RedColor,
			))
	}

	primaryUserIntent := primaryCluster.GetUserIntent()
	primaryDeviceInfo := primaryUserIntent.GetDeviceInfo()
	primaryK8sTserverResourceSpec := primaryUserIntent.GetTserverK8SNodeResourceSpec()
	primaryK8sMasterResourceSpec := primaryUserIntent.GetMasterK8SNodeResourceSpec()

	var rootCA, clientRootCA string
	if details.GetClientRootCA() == "" {
		clientRootCA = details.GetRootCA()
	}

	for _, cert := range universe.Certificates {
		if cert.GetUuid() == details.GetRootCA() {
			rootCA = cert.GetLabel()
		}
		if cert.GetUuid() == details.GetClientRootCA() {
			clientRootCA = cert.GetLabel()
		}
	}

	var earKMSConfig string
	kms := details.GetEncryptionAtRestConfig()
	for _, k := range universe.KMSConfigs {
		if !util.IsEmptyString(k.ConfigUUID) &&
			strings.Compare(k.ConfigUUID, kms.GetKmsConfigUUID()) == 0 {
			earKMSConfig = k.Name
			break
		}
	}

	enableVolumeEncryption := false
	if len(earKMSConfig) != 0 {
		enableVolumeEncryption = true
	}

	var cveKMSConfig string
	cveKMSConfigBlock := primaryDeviceInfo.GetCloudVolumeEncryption()
	for _, k := range universe.KMSConfigs {
		if !util.IsEmptyString(k.ConfigUUID) &&
			strings.Compare(k.ConfigUUID, cveKMSConfigBlock.GetKmsConfigUUID()) == 0 {
			cveKMSConfig = k.Name
			break
		}
	}

	var provider ybaclient.Provider
	for _, p := range universe.Providers {
		if p.GetUuid() == primaryUserIntent.GetProvider() {
			provider = p
			break
		}
	}

	regionsInProvider := provider.GetRegions()
	regionsInPrimaryCluster := primaryUserIntent.GetRegionList()
	outputRegions := make([]string, 2)

	outputPrimaryRegions := ""
	for i, r := range regionsInPrimaryCluster {
		for _, rInProvider := range regionsInProvider {
			if strings.Compare(r, rInProvider.GetUuid()) == 0 {
				if i == 0 {
					outputPrimaryRegions = rInProvider.GetCode()
				} else {
					outputPrimaryRegions = fmt.Sprintf("%s,%s", outputPrimaryRegions, rInProvider.GetCode())
				}
			}
		}
	}

	outputRegions[0] = outputPrimaryRegions

	outputPreferredRegions := make([]string, 2)

	outputImageBundles := make([]string, 2)

	for _, ibInProvider := range provider.GetImageBundles() {
		if strings.Compare(primaryUserIntent.GetImageBundleUUID(), ibInProvider.GetUuid()) == 0 {
			outputImageBundles[0] = ibInProvider.GetName()
			break
		}
	}

	for _, rInProvider := range regionsInProvider {
		if strings.Compare(primaryUserIntent.GetPreferredRegion(), rInProvider.GetUuid()) == 0 {
			outputPreferredRegions[0] = rInProvider.GetCode()
			break
		}
	}

	var rrUserIntent ybaclient.UserIntent
	var rrDeviceInfo ybaclient.DeviceInfo
	var rrK8sTserverResourceSpec ybaclient.K8SNodeResourceSpec
	rrClusterExists := false
	if !IsClusterEmpty(rrCluster) {
		rrClusterExists = true
		rrUserIntent = rrCluster.GetUserIntent()
		rrDeviceInfo = rrUserIntent.GetDeviceInfo()
		rrK8sTserverResourceSpec = rrUserIntent.GetTserverK8SNodeResourceSpec()

		for _, rInProvider := range regionsInProvider {
			if strings.Compare(rrUserIntent.GetPreferredRegion(), rInProvider.GetUuid()) == 0 {
				outputPreferredRegions[1] = rInProvider.GetCode()
				break
			}
		}

		for _, ibInProvider := range provider.GetImageBundles() {
			if strings.Compare(rrUserIntent.GetImageBundleUUID(), ibInProvider.GetUuid()) == 0 {
				outputImageBundles[1] = ibInProvider.GetName()
				break
			}
		}

		regionsInRRCluster := rrUserIntent.GetRegionList()
		outputRRRegions := ""
		for i, r := range regionsInRRCluster {
			for _, rInProvider := range regionsInProvider {
				if strings.Compare(r, rInProvider.GetUuid()) == 0 {
					if i == 0 {
						outputRRRegions = rInProvider.GetCode()
					} else {
						outputRRRegions = fmt.Sprintf("%s,%s", outputRRRegions, rInProvider.GetCode())
					}
				}
			}
		}
		outputRegions[1] = outputRRRegions
	}

	orderedFlags := make([]string, 0)
	flags := make(map[string]pflag.Value)
	defaultValuesOfFlags := make(map[string]string)
	flagsInCreateUniverse.VisitAll(func(f *pflag.Flag) {
		flags[f.Name] = f.Value
		defaultValuesOfFlags[f.Name] = f.DefValue
		orderedFlags = append(orderedFlags, f.Name)
	})

	_ = flags["name"].Set(u.GetName())
	_ = flags["cpu-architecture"].Set(strings.ToLower(details.GetArch()))
	_ = flags["add-read-replica"].Set(fmt.Sprintf("%t", rrClusterExists))
	_ = flags["enable-node-to-node-encrypt"].Set(
		fmt.Sprintf("%t", primaryUserIntent.GetEnableNodeToNodeEncrypt()),
	)
	_ = flags["enable-client-to-node-encrypt"].Set(
		fmt.Sprintf("%t", primaryUserIntent.GetEnableClientToNodeEncrypt()),
	)
	_ = flags["root-ca"].Set(rootCA)
	_ = flags["client-root-ca"].Set(clientRootCA)
	_ = flags["enable-volume-encryption"].Set(fmt.Sprintf("%t", enableVolumeEncryption))
	_ = flags["kms-config"].Set(earKMSConfig)
	_ = flags["provider-code"].Set(primaryUserIntent.GetProviderType())
	_ = flags["provider-name"].Set(provider.GetName())
	_ = flags["dedicated-nodes"].Set(fmt.Sprintf("%t", primaryUserIntent.GetDedicatedNodes()))

	if rf, ok := flags["replication-factor"]; ok {
		rfValues := []int32{primaryUserIntent.GetReplicationFactor()}

		if rrClusterExists {
			rfValues = append(rfValues, rrUserIntent.GetReplicationFactor())
		}

		for _, val := range rfValues {
			_ = rf.Set(fmt.Sprintf("%d", val))
		}
	}

	if nn, ok := flags["num-nodes"]; ok {
		nnValues := []int32{primaryUserIntent.GetNumNodes()}

		if rrClusterExists {
			nnValues = append(nnValues, rrUserIntent.GetNumNodes())
		}

		for _, val := range nnValues {
			_ = nn.Set(fmt.Sprintf("%d", val))
		}
	}

	if r, ok := flags["regions"]; ok {
		rValues := []string{outputRegions[0]}
		if rrClusterExists {
			rValues = append(rValues, outputRegions[1])
		}
		for _, val := range rValues {
			_ = r.Set(val)
		}
	}

	if pr, ok := flags["preferred-region"]; ok {
		prValues := []string{outputPreferredRegions[0]}
		if rrClusterExists {
			prValues = append(prValues, outputPreferredRegions[1])
		}
		for _, val := range prValues {
			_ = pr.Set(val)
		}
	}

	if ib, ok := flags["linux-version"]; ok {
		ibValues := []string{outputImageBundles[0]}
		if rrClusterExists {
			ibValues = append(ibValues, outputImageBundles[1])
		}
		for _, val := range ibValues {
			_ = ib.Set(val)
		}
	}

	if it, ok := flags["instance-type"]; ok {
		itValues := []string{primaryUserIntent.GetInstanceType()}
		if rrClusterExists {
			itValues = append(itValues, rrUserIntent.GetInstanceType())
		}
		for _, val := range itValues {
			_ = it.Set(val)
		}
	}

	if nv, ok := flags["num-volumes"]; ok {
		nvValues := []int32{primaryDeviceInfo.GetNumVolumes()}
		if rrClusterExists {
			nvValues = append(nvValues, rrDeviceInfo.GetNumVolumes())
		}
		for _, val := range nvValues {
			_ = nv.Set(fmt.Sprintf("%d", val))
		}
	}

	if vs, ok := flags["volume-size"]; ok {
		vsValues := []int32{primaryDeviceInfo.GetVolumeSize()}
		if rrClusterExists {
			vsValues = append(vsValues, rrDeviceInfo.GetVolumeSize())
		}
		for _, val := range vsValues {
			_ = vs.Set(fmt.Sprintf("%d", val))
		}
	}

	if mp, ok := flags["mount-points"]; ok {
		mpValues := []string{primaryDeviceInfo.GetMountPoints()}
		if rrClusterExists {
			mpValues = append(mpValues, rrDeviceInfo.GetMountPoints())
		}
		for _, val := range mpValues {
			_ = mp.Set(val)
		}
	}

	if st, ok := flags["storage-type"]; ok {
		stValues := []string{primaryDeviceInfo.GetStorageType()}
		if rrClusterExists {
			stValues = append(stValues, rrDeviceInfo.GetStorageType())
		}
		for _, val := range stValues {
			_ = st.Set(val)
		}
	}

	if sc, ok := flags["storage-class"]; ok {
		scValues := []string{primaryDeviceInfo.GetStorageClass()}
		if rrClusterExists {
			scValues = append(scValues, rrDeviceInfo.GetStorageClass())
		}
		for _, val := range scValues {
			_ = sc.Set(val)
		}
	}

	if di, ok := flags["disk-iops"]; ok {
		diskIops := primaryDeviceInfo.DiskIops
		if diskIops == nil {
			diskIops = resources.Gp3FreePiops
		}
		diValues := []int32{*diskIops}

		if rrClusterExists {
			diskIops = rrDeviceInfo.DiskIops
			if diskIops == nil {
				diskIops = resources.Gp3FreePiops
			}
			diValues = append(diValues, *diskIops)
		}

		for _, val := range diValues {
			_ = di.Set(fmt.Sprintf("%d", val))
		}
	}

	if t, ok := flags["throughput"]; ok {
		throughput := primaryDeviceInfo.Throughput
		if throughput == nil {
			throughput = resources.Gp3FreeThroughput
		}
		tValues := []int32{*throughput}

		if rrClusterExists {
			throughput = rrDeviceInfo.Throughput
			if throughput == nil {
				throughput = resources.Gp3FreeThroughput
			}
			tValues = append(tValues, *throughput)
		}

		for _, val := range tValues {
			_ = t.Set(fmt.Sprintf("%d", val))
		}
	}

	if strings.EqualFold(primaryUserIntent.GetProviderType(), util.K8sProviderType) {
		if k8sMemServer, ok := flags["k8s-tserver-mem-size"]; ok {
			k8sMemServerValues := []float64{primaryK8sTserverResourceSpec.GetMemoryGib()}
			if rrClusterExists {
				k8sMemServerValues = append(
					k8sMemServerValues,
					rrK8sTserverResourceSpec.GetMemoryGib(),
				)
			}
			for _, val := range k8sMemServerValues {
				_ = k8sMemServer.Set(fmt.Sprintf("%0.2f", val))
			}
		}

		if k8sMemCPUCount, ok := flags["k8s-tserver-cpu-core-count"]; ok {
			k8sMemCPUCountValues := []float64{primaryK8sTserverResourceSpec.GetCpuCoreCount()}
			if rrClusterExists {
				k8sMemCPUCountValues = append(
					k8sMemCPUCountValues,
					rrK8sTserverResourceSpec.GetCpuCoreCount(),
				)
			}
			for _, val := range k8sMemCPUCountValues {
				_ = k8sMemCPUCount.Set(fmt.Sprintf("%0.2f", val))
			}
		}

		_ = flags["k8s-master-mem-size"].Set(
			fmt.Sprintf("%0.2f", primaryK8sMasterResourceSpec.GetMemoryGib()),
		)
		_ = flags["k8s-master-cpu-core-count"].Set(
			fmt.Sprintf("%0.2f", primaryK8sMasterResourceSpec.GetCpuCoreCount()),
		)

		directoryPath, perms, _ := util.GetCLIConfigDirectoryPath()
		if len(directoryPath) == 0 {
			directoryPath = "."
			perms = 0644
		}

		universeOverrides := primaryUserIntent.GetUniverseOverrides()
		if len(universeOverrides) > 0 {
			filePath := filepath.Join(
				directoryPath,
				fmt.Sprintf("%s-universe-overrides.yaml", u.GetName()),
			)
			success, err := util.StringToYAMLFile(universeOverrides, filePath, perms)
			if success && err == nil {
				_ = flags["kubernetes-universe-overrides-file-path"].Set(filePath)
			}
		}

		zonesOverrides := primaryUserIntent.GetAzOverrides()
		if len(zonesOverrides) > 0 {

			for zone, overrides := range zonesOverrides {
				filePath := filepath.Join(
					directoryPath,
					fmt.Sprintf("%s-%s-az-overrides.yaml", u.GetName(), zone),
				)
				fullYAML := fmt.Sprintf("%s:\n%s", zone, overrides)
				success, err := util.StringToYAMLFile(fullYAML, filePath, perms)
				if success && err == nil {
					_ = flags["kubernetes-az-overrides-file-path"].Set(filePath)
				}
			}
		}
	}

	if exposingService, ok := flags["exposing-service"]; ok {
		exposingServiceValues := []string{
			strings.ToLower(primaryUserIntent.GetEnableExposingService()),
		}

		if rrClusterExists {
			exposingServiceValues = append(
				exposingServiceValues,
				strings.ToLower(rrUserIntent.GetEnableExposingService()),
			)
		}
		for _, val := range exposingServiceValues {
			_ = exposingService.Set(val)
		}
	}

	if primaryUserIntent.GetDedicatedNodes() {
		primaryMasterDeviceInfo := primaryUserIntent.GetMasterDeviceInfo()
		if IsDeviceInfoEmpty(primaryMasterDeviceInfo) {
			primaryMasterDeviceInfo = primaryUserIntent.GetDeviceInfo()
		}

		_ = flags["dedicated-master-instance-type"].Set(primaryUserIntent.GetMasterInstanceType())
		_ = flags["dedicated-master-num-volumes"].Set(
			fmt.Sprintf("%d", primaryMasterDeviceInfo.GetNumVolumes()),
		)
		_ = flags["dedicated-master-volume-size"].Set(
			fmt.Sprintf("%d", primaryMasterDeviceInfo.GetVolumeSize()),
		)
		_ = flags["dedicated-master-mount-points"].Set(primaryMasterDeviceInfo.GetMountPoints())
		_ = flags["dedicated-master-storage-type"].Set(primaryMasterDeviceInfo.GetStorageType())
		_ = flags["dedicated-master-storage-class"].Set(primaryMasterDeviceInfo.GetStorageClass())
		_ = flags["dedicated-master-disk-iops"].Set(
			fmt.Sprintf("%d", primaryMasterDeviceInfo.GetDiskIops()),
		)
		_ = flags["dedicated-master-throughput"].Set(
			fmt.Sprintf("%d", primaryMasterDeviceInfo.GetThroughput()),
		)
	}

	_ = flags["use-spot-instance"].Set(fmt.Sprintf("%t", primaryUserIntent.GetUseSpotInstance()))
	_ = flags["spot-price"].Set(fmt.Sprintf("%f", primaryUserIntent.GetSpotPrice()))

	_ = flags["assign-public-ip"].Set(fmt.Sprintf("%t", primaryUserIntent.GetAssignPublicIP()))
	_ = flags["assign-static-public-ip"].Set(
		fmt.Sprintf("%t", primaryUserIntent.GetAssignStaticPublicIP()),
	)
	_ = flags["enable-ysql"].Set(fmt.Sprintf("%t", primaryUserIntent.GetEnableYSQL()))
	_ = flags["ysql-password"].Set(primaryUserIntent.GetYsqlPassword())
	_ = flags["enable-ycql"].Set(fmt.Sprintf("%t", primaryUserIntent.GetEnableYCQL()))
	_ = flags["ycql-password"].Set(primaryUserIntent.GetYcqlPassword())
	_ = flags["enable-yedis"].Set(fmt.Sprintf("%t", primaryUserIntent.GetEnableYEDIS()))
	_ = flags["enable-ipv6"].Set(fmt.Sprintf("%t", primaryUserIntent.GetEnableIPV6()))
	_ = flags["yb-db-version"].Set(primaryUserIntent.GetYbSoftwareVersion())
	_ = flags["use-systemd"].Set(fmt.Sprintf("%t", primaryUserIntent.GetUseSystemd()))
	_ = flags["access-key-code"].Set(primaryUserIntent.GetAccessKeyCode())
	_ = flags["aws-arn-string"].Set(primaryUserIntent.GetAwsArnString())

	for k, v := range primaryUserIntent.GetInstanceTags() {
		_ = flags["user-tags"].Set(fmt.Sprintf("%s=%s", k, v))
	}

	communicationPorts := details.GetCommunicationPorts()

	_ = flags["master-http-port"].Set(fmt.Sprintf("%d", communicationPorts.GetMasterHttpPort()))
	_ = flags["master-rpc-port"].Set(fmt.Sprintf("%d", communicationPorts.GetMasterRpcPort()))
	_ = flags["tserver-http-port"].Set(fmt.Sprintf("%d", communicationPorts.GetTserverHttpPort()))
	_ = flags["tserver-rpc-port"].Set(fmt.Sprintf("%d", communicationPorts.GetTserverRpcPort()))
	_ = flags["node-exporter-port"].Set(fmt.Sprintf("%d", communicationPorts.GetNodeExporterPort()))
	_ = flags["redis-server-http-port"].Set(
		fmt.Sprintf("%d", communicationPorts.GetRedisServerHttpPort()),
	)
	_ = flags["redis-server-rpc-port"].Set(
		fmt.Sprintf("%d", communicationPorts.GetRedisServerRpcPort()),
	)
	_ = flags["yql-server-http-port"].Set(
		fmt.Sprintf("%d", communicationPorts.GetYqlServerHttpPort()),
	)
	_ = flags["yql-server-rpc-port"].Set(
		fmt.Sprintf("%d", communicationPorts.GetYqlServerRpcPort()),
	)
	_ = flags["ysql-server-http-port"].Set(
		fmt.Sprintf("%d", communicationPorts.GetYsqlServerHttpPort()),
	)
	_ = flags["ysql-server-rpc-port"].Set(
		fmt.Sprintf("%d", communicationPorts.GetYsqlServerRpcPort()),
	)

	if util.IsFeatureFlagEnabled(util.PREVIEW) {
		_ = flags["internal-ysql-server-rpc-port"].Set(
			fmt.Sprintf("%d", communicationPorts.GetInternalYsqlServerRpcPort()),
		)
		if primaryUserIntent.GetEnableConnectionPooling() {
			_ = flags["connection-pooling"].Set("enable")
		} else {
			_ = flags["connection-pooling"].Set("disable")
		}
		_ = flags["encryption-at-rest-kms-config"].Set(earKMSConfig)
		_ = flags["cloud-volume-encryption-kms-config"].Set(cveKMSConfig)
	}

	primarySpecificGFlags := primaryUserIntent.GetSpecificGFlags()
	primaryPerProcessGFlags := primarySpecificGFlags.GetPerProcessFlags()
	primaryValues := primaryPerProcessGFlags.GetValue()

	masterGFlags := primaryValues[util.MasterServerType]
	primaryTserverGFlags := primaryValues[util.TserverServerType]

	outputMap := make(map[string]interface{})

	if len(masterGFlags) == 0 {
		_ = flags["master-gflags"].Set("")
	} else if strings.EqualFold(outputFormat, "yaml") {

		gflagsYAML, err := yaml.Marshal(masterGFlags)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		_ = flags["master-gflags"].Set(string(gflagsYAML))
	} else {
		gflagsJSON, err := json.Marshal(masterGFlags)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		_ = flags["master-gflags"].Set(string(gflagsJSON))
	}
	if len(masterGFlags) != 0 {
		outputMap["master-gflags"] = masterGFlags
	}

	tserverGFlags := make(map[string]map[string]string)

	if len(primaryTserverGFlags) != 0 {
		tserverGFlags[strings.ToLower(util.PrimaryClusterType)] = primaryTserverGFlags
	}

	if rrClusterExists {
		rrSpecificCGFlags := rrUserIntent.GetSpecificGFlags()
		if !rrSpecificCGFlags.GetInheritFromPrimary() {
			rrPerProcessGFlags := rrSpecificCGFlags.GetPerProcessFlags()
			rrValues := rrPerProcessGFlags.GetValue()
			rrTserverGFlags := rrValues[util.TserverServerType]
			if len(rrTserverGFlags) != 0 {
				tserverGFlags[strings.ToLower(util.ReadReplicaClusterType)] = rrTserverGFlags
			}
		}
	}

	if len(primaryTserverGFlags) == 0 {
		_ = flags["tserver-gflags"].Set("")
	} else if strings.EqualFold(outputFormat, "yaml") {
		gflagsYAML, err := yaml.Marshal(tserverGFlags)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		_ = flags["tserver-gflags"].Set(string(gflagsYAML))

	} else {
		gflagsJSON, err := json.Marshal(tserverGFlags)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		_ = flags["tserver-gflags"].Set(string(gflagsJSON))
	}
	if len(tserverGFlags) != 0 {
		outputMap["tserver-gflags"] = tserverGFlags
	}

	if strings.EqualFold(outputFormat, "flag") {
		for _, k := range orderedFlags {
			flagValue := flags[k]
			flagValueString := flagValue.String()
			if len(flagValueString) == 0 {
				continue
			}
			if len(defaultValuesOfFlags[k]) != 0 && flagValueString == defaultValuesOfFlags[k] {
				continue
			}
			if strings.EqualFold(flagValueString, "true") {
				fmt.Printf("--%s ", k)
				continue
			}
			if strings.EqualFold(flagValueString, "false") {
				fmt.Printf("--%s=false ", k)
				continue
			}
			if sliceVal, ok := flags[k].(pflag.SliceValue); ok {
				for _, val := range sliceVal.GetSlice() {
					if _, err := strconv.Atoi(val); err == nil {
						fmt.Printf("--%s %s ", k, val)
						continue
					}

					if _, err := strconv.ParseFloat(val, 64); err == nil {
						fmt.Printf("--%s %s ", k, val)
						continue
					}
					if len(val) == 0 {
						continue
					}
					fmt.Printf("--%s %q ", k, val)
				}
				continue
			}

			if strings.Contains(k, "password") || strings.Contains(k, "gflags") {
				fmt.Printf("--%s '%s' ", k, flagValueString)
				continue
			}
			fmt.Printf("--%s %v ", k, flags[k])

		}
		fmt.Println()
	} else {
		for _, k := range orderedFlags {
			flagValue := flags[k]
			flagValueString := flagValue.String()
			if len(flagValueString) == 0 {
				continue
			}
			if len(defaultValuesOfFlags[k]) != 0 && flagValueString == defaultValuesOfFlags[k] {
				continue
			}
			if sliceVal, ok := flags[k].(pflag.SliceValue); ok {
				var sliceValues []interface{}
				for _, val := range sliceVal.GetSlice() {
					if intValue, err := strconv.Atoi(val); err == nil {
						sliceValues = append(sliceValues, intValue)
					} else if floatValue, err := strconv.ParseFloat(val, 64); err == nil {
						sliceValues = append(sliceValues, floatValue)
					} else if len(val) != 0 {
						sliceValues = append(sliceValues, val)
					}
				}
				if len(sliceValues) > 0 {
					outputMap[k] = sliceValues
				}
				continue
			}
			if k == "user-tags" {
				mapValues := make(map[string]string)
				flagValueString = strings.Trim(flagValueString, "[]")

				for _, val := range strings.Split(flagValueString, ",") {
					kvp := strings.SplitN(val, "=", 2)
					if len(kvp) == 2 {
						mapValues[strings.TrimSpace(kvp[0])] = strings.TrimSpace(kvp[1])
					}
				}
				outputMap[k] = mapValues
				continue
			}
			if strings.Contains(k, "gflags") {
				continue
			}
			outputMap[k] = flagValue
		}
		if strings.EqualFold(outputFormat, "yaml") {
			yamlOutput, err := yaml.Marshal(outputMap)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			fmt.Println(string(yamlOutput))
		} else {
			jsonOutput, err := json.Marshal(outputMap)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			fmt.Println(string(jsonOutput))
		}
	}
}
