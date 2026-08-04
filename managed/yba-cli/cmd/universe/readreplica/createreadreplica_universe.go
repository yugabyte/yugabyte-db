/*
 * Copyright (c) YugabyteDB, Inc.
 */

package readreplica

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

// CreateReadReplicaUniverseCmd represents the universe command
var CreateReadReplicaUniverseCmd = &cobra.Command{
	Use:     "create-read-replica",
	Aliases: []string{"add-read-replica"},
	GroupID: "read-replica",
	Short:   "Create a read replica for existing YugabyteDB universe",
	Long:    "Create a read replica for existing YugabyteDB universe",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to add read replica to\n",
					formatter.RedColor))
		}
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.EditOperation)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI, universeUsed, err := universeutil.Validations(cmd, "Create Read Only Cluster")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeUUID := universeUsed.GetUniverseUUID()
		universeName := universeUsed.GetName()
		universeDetails := universeUsed.GetUniverseDetails()
		arch := universeDetails.GetArch()
		clusters := universeDetails.GetClusters()
		if len(clusters) < 1 {
			err := fmt.Errorf(
				"No clusters found in universe " + universeName + " (" + universeUUID + ")")
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		primaryCluster := universeutil.FindClusterByType(clusters, util.PrimaryClusterType)
		if universeutil.IsClusterEmpty(primaryCluster) {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"No primary cluster found in universe %s (%s)\n",
						universeName,
						universeUUID,
					),
					formatter.RedColor,
				))
		}
		primaryUserIntent := primaryCluster.GetUserIntent()

		providerUsedUUID := primaryUserIntent.GetProvider()

		providerRegions, regions, preferredRegion, imageBundle, err := getRegionsAndImageBundle(
			authAPI,
			cmd,
			primaryUserIntent,
			arch,
			universeName,
			universeUUID,
			providerUsedUUID,
		)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		cloudList := make([]ybaclient.PlacementCloud, 0)

		cloud := ybaclient.PlacementCloud{
			Uuid: util.GetStringPointer(providerUsedUUID),
			Code: primaryUserIntent.ProviderType,
		}
		cloudList = append(cloudList, cloud)

		zonesString, err := cmd.Flags().GetStringArray("zones")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		cloudList, err = universeutil.RemoveOrAddZones(
			cloudList,
			providerRegions,
			providerUsedUUID,
			regions,
			nil,
			nil,
			zonesString,
		)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		deviceInfo, err := getDeviceInfo(cmd, primaryUserIntent)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rf, err := cmd.Flags().GetInt("replication-factor")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		node, err := cmd.Flags().GetInt("num-nodes")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		userTagsMap, err := cmd.Flags().GetStringToString("user-tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(userTagsMap) == 0 {
			userTagsMap = primaryUserIntent.GetInstanceTags()
		}

		tserverGFlagsString, err := cmd.Flags().GetString("tserver-gflags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var tserverGFlags map[string]string

		specificGFlags := ybaclient.SpecificGFlags{
			InheritFromPrimary: util.GetBoolPointer(false),
			PerProcessFlags: &ybaclient.PerProcessFlags{
				Value: map[string]map[string]string{
					util.MasterServerType:  {},
					util.TserverServerType: {},
				},
			},
		}

		if !util.IsEmptyString(tserverGFlagsString) {
			if strings.HasPrefix(strings.TrimSpace(tserverGFlagsString), "{") {
				tserverGFlags = universeutil.ProcessGFlagsJSONString(tserverGFlagsString, "Tserver")
			} else {
				// Assume YAML format
				tserverGFlags = universeutil.ProcessGFlagsYAMLString(tserverGFlagsString, "Tserver")
			}
		}
		if len(tserverGFlags) == 0 {
			specificGFlags.SetInheritFromPrimary(true)
		} else {
			preProcessFlags := specificGFlags.GetPerProcessFlags()
			value := preProcessFlags.GetValue()
			value[util.TserverServerType] = tserverGFlags
			preProcessFlags.SetValue(value)
			specificGFlags.SetPerProcessFlags(preProcessFlags)
		}

		exposingServices, err := cmd.Flags().GetString("exposing-service")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(exposingServices) == 0 {
			exposingServices = primaryUserIntent.GetEnableExposingService()
		}

		instanceType, err := cmd.Flags().GetString("instance-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(instanceType) == 0 {
			instanceType = primaryUserIntent.GetInstanceType()
		}

		var k8sTserverMemSize float64
		var k8sTserverCPUCoreCount float64
		if cmd.Flags().Changed("k8s-tserver-mem-size") {
			k8sTserverMemSize, err = cmd.Flags().GetFloat64("k8s-tserver-mem-size")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		} else {
			spec := primaryUserIntent.GetTserverK8SNodeResourceSpec()
			k8sTserverMemSize = spec.GetMemoryGib()
		}

		if cmd.Flags().Changed("k8s-tserver-cpu-core-count") {
			k8sTserverCPUCoreCount, err = cmd.Flags().GetFloat64("k8s-tserver-cpu-core-count")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		} else {
			spec := primaryUserIntent.GetTserverK8SNodeResourceSpec()
			k8sTserverCPUCoreCount = spec.GetCpuCoreCount()
		}

		rrUserIntent := ybaclient.UserIntent{
			UniverseName:   util.GetStringPointer(universeName),
			ProviderType:   primaryUserIntent.ProviderType,
			Provider:       primaryUserIntent.Provider,
			DedicatedNodes: util.GetBoolPointer(false),

			InstanceType: util.GetStringPointer(instanceType),
			DeviceInfo:   &deviceInfo,

			AssignPublicIP:          primaryUserIntent.AssignPublicIP,
			AssignStaticPublicIP:    primaryUserIntent.AssignStaticPublicIP,
			EnableYSQL:              primaryUserIntent.EnableYSQL,
			YsqlPassword:            primaryUserIntent.YsqlPassword,
			EnableConnectionPooling: primaryUserIntent.EnableConnectionPooling,
			EnableYSQLAuth:          primaryUserIntent.EnableYSQLAuth,
			EnableYCQL:              primaryUserIntent.EnableYCQL,
			YcqlPassword:            primaryUserIntent.YcqlPassword,
			EnableYCQLAuth:          primaryUserIntent.EnableYCQLAuth,
			EnableYEDIS:             primaryUserIntent.EnableYEDIS,

			EnableClientToNodeEncrypt: primaryUserIntent.EnableClientToNodeEncrypt,
			EnableNodeToNodeEncrypt:   primaryUserIntent.EnableNodeToNodeEncrypt,

			UseSystemd:        primaryUserIntent.UseSystemd,
			YbSoftwareVersion: primaryUserIntent.YbSoftwareVersion,
			AccessKeyCode:     primaryUserIntent.AccessKeyCode,
			EnableIPV6:        primaryUserIntent.EnableIPV6,
			InstanceTags:      util.StringtoStringMap(userTagsMap),

			ReplicationFactor: util.GetInt32Pointer(int32(rf)),
			NumNodes:          util.GetInt32Pointer(int32(node)),
			RegionList:        regions,
			PreferredRegion:   util.GetStringPointer(preferredRegion),
			AwsArnString:      primaryUserIntent.AwsArnString,

			TserverGFlags:  util.StringtoStringMap(tserverGFlags),
			SpecificGFlags: &specificGFlags,

			EnableExposingService: util.GetStringPointer(exposingServices),
		}
		if primaryUserIntent.GetProviderType() == util.K8sProviderType {
			rrUserIntent.SetTserverK8SNodeResourceSpec(ybaclient.K8SNodeResourceSpec{
				MemoryGib:    k8sTserverMemSize,
				CpuCoreCount: k8sTserverCPUCoreCount,
			})
		}

		if util.IsCloudBasedProvider(primaryUserIntent.GetProviderType()) {
			rrUserIntent.SetImageBundleUUID(imageBundle)
			rrUserIntent.SetUseSpotInstance(primaryUserIntent.GetUseSpotInstance())
			rrUserIntent.SetSpotPrice(primaryUserIntent.GetSpotPrice())
		}
		index := int32(len(clusters) + 1)
		rrCluster := ybaclient.Cluster{
			ClusterType: util.ReadReplicaClusterType,
			UserIntent:  rrUserIntent,
			Index:       util.GetInt32Pointer(index),
			PlacementInfo: &ybaclient.PlacementInfo{
				CloudList: cloudList,
			},
		}

		reqClusters := []ybaclient.Cluster{rrCluster}
		req := ybaclient.UniverseDefinitionTaskParams{
			Arch:                   universeDetails.Arch,
			ClientRootCA:           universeDetails.ClientRootCA,
			Clusters:               reqClusters,
			CommunicationPorts:     universeDetails.CommunicationPorts,
			DeviceInfo:             &deviceInfo,
			EnableYbc:              universeDetails.EnableYbc,
			EncryptionAtRestConfig: universeDetails.EncryptionAtRestConfig,
			NodeDetailsSet: universeutil.BuildNodeDetailsRespArrayToNodeDetailsArray(
				universeDetails.GetNodeDetailsSet()),
			UniverseUUID:            util.GetStringPointer(universeUUID),
			RootAndClientRootCASame: universeDetails.RootAndClientRootCASame,
			RootCA:                  universeDetails.RootCA,
		}

		createRR := authAPI.CreateReadOnlyCluster(universeUUID).UniverseConfigureTaskParams(req)
		rTask, response, err := createRR.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Create Read Only Cluster")
		}

		util.CheckTaskAfterCreation(rTask)

		msg := fmt.Sprintf("The read replica for universe %s is being created",
			formatter.Colorize(universeName, formatter.GreenColor))

		if viper.GetBool("wait") {
			if rTask.TaskUUID != nil {
				logrus.Info(fmt.Sprintf("Waiting for read replica for "+
					"universe %s (%s) to be created\n",
					formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The read replica for universe %s (%s) has been created\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
			universeData, response, err := authAPI.ListUniverses().Name(universeName).Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Universe", "Create - Fetch Universe")
			}

			universesCtx := formatter.Context{
				Command: "create",
				Output:  os.Stdout,
				Format:  universe.NewUniverseFormat(viper.GetString("output")),
			}

			universe.Write(universesCtx, universeData)
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})

	},
}

func init() {
	CreateReadReplicaUniverseCmd.Flags().SortFlags = false
	CreateReadReplicaUniverseCmd.Flags().
		StringP("name", "n", "", "[Required] Name of the universe to add read replica to.")
	CreateReadReplicaUniverseCmd.MarkFlagRequired("name")

	CreateReadReplicaUniverseCmd.Flags().Int("replication-factor", 3,
		"[Optional] Number of read replicas to be added.")
	CreateReadReplicaUniverseCmd.Flags().Int("num-nodes", 3,
		"[Optional] Number of nodes in the read replica universe.")
	CreateReadReplicaUniverseCmd.Flags().String("regions", "",
		"[Optional] Regions to add read replica to. Defaults to primary cluster regions.")
	CreateReadReplicaUniverseCmd.Flags().String("preferred-region", "",
		"[Optional] Preferred region to place the node of the cluster in.")

	CreateReadReplicaUniverseCmd.Flags().StringArray("zones", []string{},
		"[Optional] Zones to add read replica nodes to. Defaults to primary cluster zones. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"--zones 'zone-name=<zone1>::region-name=<region1>::num-nodes=<number-of-nodes-to-be-placed-in-zone>\" "+
			"Each zone must have the region and number of nodes to be placed in that zone. "+
			"Add the region via --regions flag if not present in the universe. "+
			"Each zone needs to be added using a separate --zones flag.")

	CreateReadReplicaUniverseCmd.Flags().String("tserver-gflags", "",
		"[Optional] TServer GFlags in map (JSON or YAML) format. "+
			"Provide the gflags in the following formats: "+
			"\"--tserver-gflags {\"tserver-gflag-key-1\":\"value-1\","+
			"\"tserver-gflag-key-2\":\"value-2\" }\" or"+
			"  \"--tserver-gflags \"tserver-gflag-key-1: value-1\ntserver-gflag-key-2"+
			": value-2\ntserver-gflag-key-3: value-3\". If no tserver gflags are provided"+
			" for the read replica, the primary cluster gflags are "+
			"by default applied to the read replica cluster.")

	CreateReadReplicaUniverseCmd.Flags().String("linux-version", "",
		"[Optional] Linux version to be used for the read replica cluster. "+
			"Default linux version is fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().String("instance-type", "",
		"[Optional] Instance type to be used for the read replica cluster. "+
			"Default instance type is fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().Int("num-volumes", 1,
		"[Optional] Number of volumes to be mounted on this instance at the default path. "+
			"Default number of volumes is fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().Int("volume-size", 100,
		"[Optional] The size of each volume in each instance."+
			"Default volume size is fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().String("mount-points", "",
		"[Optional] Disk mount points. "+
			"Default disk mount points are fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().String("storage-type", "",
		"[Optional] Storage type (EBS for AWS) used for this instance. "+
			"Default storage type is fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().String("storage-class", "",
		"[Optional]  Name of the storage class, supported for Kubernetes. "+
			"Default storage class is fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().Int("disk-iops", 3000,
		"[Optional] Desired IOPS for the volumes mounted on this instance,"+
			" supported only for AWS. Default disk IOPS is fetched from the primary cluster.")
	CreateReadReplicaUniverseCmd.Flags().Int("throughput", 125,
		"[Optional] Desired throughput for the volumes mounted on this instance in MB/s, "+
			"supported only for AWS. Default throughput is fetched from the primary cluster.")
	CreateReadReplicaUniverseCmd.Flags().Float64("k8s-tserver-mem-size", 4,
		"[Optional] Memory size of the kubernetes tserver node in GB."+
			" Default memory size is fetched from the primary cluster.")
	CreateReadReplicaUniverseCmd.Flags().Float64("k8s-tserver-cpu-core-count", 2,
		"[Optional] CPU core count of the kubernetes tserver node."+
			" Default CPU core count is fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().String("exposing-service", "",
		"[Optional] Exposing service for the universe clusters. "+
			"Allowed values: none, exposed, unexposed. "+
			"Default exposing service is fetched from the primary cluster.")

	CreateReadReplicaUniverseCmd.Flags().StringToString("user-tags",
		map[string]string{}, "[Optional] User Tags for the DB instances. Provide "+
			"as key-value pairs per flag. Example \"--user-tags "+
			"name=test --user-tags owner=development\" OR "+
			"\"--user-tags name=test,owner=development\".")

	CreateReadReplicaUniverseCmd.Flags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")
}

func getRegionsAndImageBundle(
	authAPI *ybaAuthClient.AuthAPIClient,
	cmd *cobra.Command,
	primaryUserIntent ybaclient.UserIntent,
	arch, universeName, universeUUID, providerUsedUUID string,
) ([]ybaclient.Region, []string, string, string, error) {
	var regions []string
	preferredRegion := ""
	imageBundle := ""

	providerUsed, response, err := authAPI.GetProvider(providerUsedUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"Universe",
			"Create Read Only Cluster - Get Provider",
		)
		return nil, nil, "", "", errMessage
	}

	if len(providerUsed.GetUuid()) == 0 {
		err := fmt.Errorf(
			"No provider found for universe " + universeName + " (" + universeUUID + ")",
		)
		return nil, nil, "", "", err
	}

	preferredRegion, err = cmd.Flags().GetString("preferred-region")
	if err != nil {
		return nil, nil, "", "", err
	}

	regionsString, err := cmd.Flags().GetString("regions")
	if err != nil {
		return nil, nil, "", "", err
	}
	if util.IsEmptyString(regionsString) {
		regions = primaryUserIntent.GetRegionList()
	} else {
		regions, err = universeutil.FetchRegionUUIDFromName(
			providerUsed.GetRegions(),
			regionsString,
			"add to read only cluster",
		)
		if err != nil {
			return nil, nil, "", "", err
		}
	}

	imageBundleName, err := cmd.Flags().GetString("linux-version")
	if err != nil {
		return nil, nil, "", "", err
	}
	if util.IsEmptyString(imageBundleName) {
		imageBundle = primaryUserIntent.GetImageBundleUUID()
	} else {
		imageBundlesInProvider := providerUsed.GetImageBundles()
		for _, ib := range imageBundlesInProvider {
			details := ib.GetDetails()
			if strings.Compare(imageBundleName, ib.GetName()) == 0 && strings.EqualFold(details.GetArch(), arch) {
				imageBundle = ib.GetUuid()
				break
			}
		}
	}
	return providerUsed.GetRegions(), regions, preferredRegion, imageBundle, nil
}

func getDeviceInfo(
	cmd *cobra.Command,
	primaryUserIntent ybaclient.UserIntent,
) (ybaclient.DeviceInfo, error) {

	primaryDeviceInfo := primaryUserIntent.GetDeviceInfo()

	var diskIops *int32
	if cmd.Flags().Changed("disk-iops") {
		diskIopsFlag, err := cmd.Flags().GetInt("disk-iops")
		if err != nil {
			return ybaclient.DeviceInfo{}, err
		}
		diskIops = util.GetInt32Pointer(int32(diskIopsFlag))
	} else {
		diskIops = primaryDeviceInfo.DiskIops
	}

	var numVolumes *int32
	if cmd.Flags().Changed("num-volumes") {
		numVolumesFlag, err := cmd.Flags().GetInt("num-volumes")
		if err != nil {
			return ybaclient.DeviceInfo{}, err
		}
		numVolumes = util.GetInt32Pointer(int32(numVolumesFlag))
	} else {
		numVolumes = primaryDeviceInfo.NumVolumes
	}

	var volumeSize *int32
	if cmd.Flags().Changed("volume-size") {
		volumeSizeFlag, err := cmd.Flags().GetInt("volume-size")
		if err != nil {
			return ybaclient.DeviceInfo{}, err
		}
		volumeSize = util.GetInt32Pointer(int32(volumeSizeFlag))
	} else {
		volumeSize = primaryDeviceInfo.VolumeSize
	}

	var storageType *string
	if cmd.Flags().Changed("storage-type") {
		storageTypeFlag, err := cmd.Flags().GetString("storage-type")
		if err != nil {
			return ybaclient.DeviceInfo{}, err
		}
		storageType = util.GetStringPointer(storageTypeFlag)
	} else {
		storageType = primaryDeviceInfo.StorageType
	}

	var storageClass *string
	if cmd.Flags().Changed("storage-class") {
		storageClassFlag, err := cmd.Flags().GetString("storage-class")
		if err != nil {
			return ybaclient.DeviceInfo{}, err
		}
		storageClass = util.GetStringPointer(storageClassFlag)
	} else {
		storageClass = primaryDeviceInfo.StorageClass
	}

	var mountPoints *string
	if cmd.Flags().Changed("mount-points") {
		mountPointsFlag, err := cmd.Flags().GetString("mount-points")
		if err != nil {
			return ybaclient.DeviceInfo{}, err
		}
		mountPoints = util.GetStringPointer(mountPointsFlag)
	} else {
		mountPoints = primaryDeviceInfo.MountPoints
	}

	var throughput *int32
	if cmd.Flags().Changed("throughput") {
		throughputFlag, err := cmd.Flags().GetInt("throughput")
		if err != nil {
			return ybaclient.DeviceInfo{}, err
		}
		throughput = util.GetInt32Pointer(int32(throughputFlag))
	} else {
		throughput = primaryDeviceInfo.Throughput
	}

	deviceInfo := ybaclient.DeviceInfo{
		DiskIops:     diskIops,
		NumVolumes:   numVolumes,
		VolumeSize:   volumeSize,
		StorageType:  storageType,
		StorageClass: storageClass,
		MountPoints:  mountPoints,
		Throughput:   throughput,
	}
	return deviceInfo, nil
}
