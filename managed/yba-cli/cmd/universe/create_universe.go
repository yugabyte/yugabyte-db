/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"fmt"
	"net/http"
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

var tserverGflagsString string
var v1 = viper.New()

// createUniverseCmd represents the universe command
var createUniverseCmd = &cobra.Command{
	Use:   "create",
	Short: "Create YugabyteDB Anywhere universe",
	Long:  "Create an universe in YugabyteDB Anywhere",
	Example: `yba universe create -n <universe-name> --provider-code <provider-code> \
	--provider-name <provider-name> --yb-db-version <YugbayteDB-version> \
	--master-gflags \
	"{\"<gflag-1>\": \"<value-1>\",\"<gflag-2>\": \"<value-2>\",\
	\"<gflag-3>\": \"<value-3>\",\"<gflag-4>\": \"<value-4>\"}" \
	--tserver-gflags \
	"{\"primary\": {\"<gflag-1>\": \"<value-1>\",\"<gflag-2>\": \"<value-2>\"},\
	\"async\": {\"<gflag-1>\": \"<value-1>\",\"<gflag-2>\": \"<value-2>\"}}" \
	--num-nodes 1 --replication-factor 1 \
	--user-tags <tag1>=<value1>,<tag2>=<value2>`,
	PreRun: func(cmd *cobra.Command, args []string) {

		config, err := cmd.Flags().GetString("config-template")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		initializeViper(config)

		universeName := v1.GetString("name")

		if len(strings.TrimSpace(universeName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to create\n", formatter.RedColor))
		}
		enableVolumeEncryption := v1.GetBool("enable-volume-encryption")
		if enableVolumeEncryption {
			cmd.MarkFlagRequired("kms-config")
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		var response *http.Response
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		config, err := cmd.Flags().GetString("config-template")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		initializeViper(config)

		universeName := v1.GetString("name")

		allowed, version, err := authAPI.UniverseYBAVersionCheck()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !allowed {
			logrus.Fatalf(formatter.Colorize(
				fmt.Sprintf("Creating universes below version %s (or on restricted"+
					" versions) is not supported, currently on %s\n", util.YBAAllowUniverseMinVersion,
					version), formatter.RedColor))
		}

		enableYbc := true
		communicationPorts := buildCommunicationPorts(cmd)

		certUUID := ""
		clientRootCA := v1.GetString("root-ca")

		// find the root certficate UUID from the name
		if len(clientRootCA) != 0 {
			certs, response, err := authAPI.GetListOfCertificates().Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err,
					"Universe", "Create - List Certificates")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
			for _, c := range certs {
				if strings.Compare(c.GetLabel(), clientRootCA) == 0 {
					certUUID = c.GetUuid()
					logrus.Info("Using certificate: ",
						fmt.Sprintf("%s %s",
							clientRootCA,
							formatter.Colorize(certUUID, formatter.GreenColor)), "\n")
				}
			}
		}

		kmsConfigUUID := ""
		var opType string
		enableVolumeEncryption := v1.GetBool("enable-volume-encryption")

		if enableVolumeEncryption {
			opType = util.EnableKMSOpType
			kmsConfigName, err := cmd.Flags().GetString("kms-config")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			// find kmsConfigUUID from the name
			kmsConfigs, response, err := authAPI.ListKMSConfigs().Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err,
					"Universe", "Create - Fetch KMS Configs")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
			for _, k := range kmsConfigs {
				metadataInterface := k["metadata"]
				if metadataInterface != nil {
					metadata := metadataInterface.(map[string]interface{})
					kmsName := metadata["name"]
					if kmsName != nil && strings.Compare(kmsName.(string), kmsConfigName) == 0 {
						configUUID := metadata["configUUID"]
						if configUUID != nil {
							kmsConfigUUID = configUUID.(string)
							logrus.Info("Using kms config: ",
								fmt.Sprintf("%s %s",
									kmsConfigName,
									formatter.Colorize(kmsConfigUUID, formatter.GreenColor)), "\n")
						}
					}
				}
			}
		}

		cpuArch := v1.GetString("cpu-architecture")
		if len(strings.TrimSpace(cpuArch)) == 0 {
			cpuArch = util.X86_64
		}

		clusters, err := buildClusters(cmd, authAPI, universeName)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.UniverseConfigureTaskParams{
			ClientRootCA:       util.GetStringPointer(certUUID),
			Clusters:           clusters,
			CommunicationPorts: communicationPorts,
			EnableYbc:          util.GetBoolPointer(enableYbc),
			Arch:               util.GetStringPointer(cpuArch),
		}

		if enableVolumeEncryption {
			requestBody.SetEncryptionAtRestConfig(ybaclient.EncryptionAtRestConfig{
				OpType:        util.GetStringPointer(opType),
				KmsConfigUUID: util.GetStringPointer(kmsConfigUUID),
			})
		}

		rCreate, response, err := authAPI.CreateAllClusters().
			UniverseConfigureTaskParams(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		universeUUID := rCreate.GetResourceUUID()
		taskUUID := rCreate.GetTaskUUID()

		var universeData []ybaclient.UniverseResp

		msg := fmt.Sprintf("The universe %s (%s) is being created",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

		if viper.GetBool("wait") {
			if taskUUID != "" {
				logrus.Info(fmt.Sprintf("\nWaiting for universe %s (%s) to be created\n",
					formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
				err = authAPI.WaitForTask(taskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The universe %s (%s) has been created\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

			universeData, response, err = authAPI.ListUniverses().Name(universeName).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err,
					"Universe", "Create - Fetch Universe")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			universesCtx := formatter.Context{
				Command: "create",
				Output:  os.Stdout,
				Format:  universe.NewUniverseFormat(viper.GetString("output")),
			}

			universe.Write(universesCtx, universeData)

		} else {
			logrus.Infoln(msg + "\n")
		}

	},
}

func init() {

	createUniverseCmd.Flags().SortFlags = false

	createUniverseCmd.Flags().String("config-template", "",
		"[Optional] Path to Universe configuration template file. "+
			"Allowed file types are json and yaml. Refer "+
			"to https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/templates for structure of config file.")

	createUniverseCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to be created.")

	createUniverseCmd.Flags().String("provider-code", "",
		"[Required] Provider code. Allowed values: aws, gcp, azu, onprem, kubernetes.")

	// fss := cliflag.NamedFlagSets{}
	// createUniverseCmd.AddGroup()

	createUniverseCmd.Flags().String("provider-name", "",
		"[Optional] Provider name to be used in universe. "+
			"Run \"yba provider list --code <provider-code>\" "+
			"to check the list of providers for the given provider-code. "+
			"Fetches the first provider in the list by default.")
	createUniverseCmd.Flags().Bool("dedicated-nodes", false,
		"[Optional] Place Masters on dedicated nodes, (default false) for aws, azu, gcp, onprem."+
			" Defaults to true for kubernetes.")
	createUniverseCmd.Flags().String("cpu-architecture", "x86_64",
		"[Optional] CPU architecture for nodes in all clusters.")
	createUniverseCmd.Flags().Bool("add-read-replica", false,
		"[Optional] Add a read replica cluster to the universe. (default false)")

	// Following fields are required individually for both primary and read replica cluster

	createUniverseCmd.Flags().IntSlice("replication-factor", []int{3, 3},
		"[Optional] Replication factor of the cluster. Provide replication-factor for each "+
			"cluster as a separate flag. \"--replication-factor 3 --replication-factor 5\" OR "+
			"\"--replication-factor 3,5\" refers to RF "+
			"of Primary cluster = 3 and RF of Read Replica = 5. First flag always corresponds to"+
			" the primary cluster.")
	createUniverseCmd.Flags().IntSlice("num-nodes", []int{3, 3},
		"[Optional] Number of nodes in the cluster. Provide no of nodes for each cluster "+
			"as a separate flag. \"--num-nodes 3 --num-nodes 5\" "+
			"OR \"--num-nodes 3,5\" "+
			"refers to 3 nodes in the Primary cluster and 5 nodes in the Read Replica cluster"+
			". First flag always corresponds to"+
			" the primry cluster.")
	createUniverseCmd.Flags().StringArray("regions", []string{},
		"[Optional] Regions for the nodes of the cluster to be placed in. "+
			"Provide comma-separated strings for each cluster as a separate flag, "+
			"in the following format: "+
			"\"--regions 'region-1-for-primary-cluster,region-2-for-primary-cluster' "+
			"--regions 'region-1-for-read-replica,region-2-for-read-replica'\". "+
			"Defaults to fetching the region from the provider. "+
			"Throws an error if multiple regions are present.")
	createUniverseCmd.Flags().StringArray("preferred-region", []string{},
		"[Optional] Preferred region to place the node of the cluster in. "+
			"Provide preferred regions for each cluster as a separate flag. Defaults to null.")

	createUniverseCmd.Flags().String("master-gflags", "",
		"[Optional] Master GFlags in map (JSON or YAML) format. "+
			"Provide the gflags in the following formats: "+
			"\"--master-gflags { \\\"master-gflag-key-1\\\":\\\"value-1\\\","+
			"\\\"master-gflag-key-2\\\":\\\"value-2\\\" }\" or"+
			"  \"--master-gflags \"master-gflag-key-1: value-1\nmaster-gflag-key-2"+
			": value-2\nmaster-gflag-key-3: value3\".")

	createUniverseCmd.Flags().StringVar(&tserverGflagsString, "tserver-gflags", "",
		"[Optional] TServer GFlags in map (JSON or YAML) format. "+
			"Provide gflags for clusters in the following format: "+
			"\"--tserver-gflags \"{\\\"primary\\\": "+
			"{\\\"tserver-gflag-key-1\\\": \\\"value-1\\\","+
			"\\\"tserver-gflag-key-2\\\": \\\"value-2\\\"},"+
			"\\\"async\\\": {\\\"tserver-gflag-key-1\\\": \\\"value-1\\\","+
			"\\\"tserver-gflag-key-2\\\": \\\"value-2\\\"}}\"\" OR"+
			" \"--tserver-gflag \"primary:\n\tlog_min_segments_to_retain: 1"+
			"\n\tlog_cache_size_limit_mb: 0\n\tglobal_log_cache_size_limit_mb: "+
			"0\n\tlog_stop_retaining_min_disk_mb: 9223372036854775807\nasync:"+
			"\n\tlog_min_segments_to_retain: 2\n\tlog_cache_size_limit_mb: 0"+
			"\n\tglobal_log_cache_size_limit_mb: 0\"\"."+
			" If no-of-clusters = 2 "+
			"and no tserver gflags are provided for the read replica, the primary cluster gflags are "+
			"by default applied to the read replica cluster.")

	// Device Info per cluster
	createUniverseCmd.Flags().StringArray("linux-version", []string{},
		"[Optional] Linux version for the universe nodes. "+
			"Default linux version is fetched from the provider. "+
			"corresponding to cpu-architecture of the universe.")
	createUniverseCmd.Flags().StringArray("instance-type", []string{},
		"[Optional] Instance Type for the universe nodes. Provide the instance types for each "+
			"cluster as a separate flag."+
			" Defaults to \"c5.large\" for aws, \"Standard_DS2_v2\" "+
			"for azure and \"n1-standard-1\" for gcp. Fetches the first available "+
			"instance type for onprem providers.")
	createUniverseCmd.Flags().IntSlice("num-volumes", []int{1, 1},
		"[Optional] Number of volumes to be mounted on this instance at the default path."+
			" Provide the number of volumes for each "+
			"cluster as a separate flag or as comma separated values.")
	createUniverseCmd.Flags().IntSlice("volume-size", []int{100, 100},
		"[Optional] The size of each volume in each instance. Provide the number of "+
			"volumes for each cluster as a separate flag or as comma separated values.")
	// Comma separated values in a single string
	createUniverseCmd.Flags().StringArray("mount-points", []string{},
		"[Optional] Disk mount points. Provide comma-separated strings for each cluster "+
			"as a separate flag, in the following format: "+
			"\"--mount-points 'mount-point-1-for-primary-cluster,mount-point-2-for-primary-cluster' "+
			"--mount-points 'mount-point-1-for-read-replica,mount-point-2-for-read-replica'\". "+
			"Defaults to null for aws, azure, gcp. Fetches the first available "+
			"instance mount points for onprem providers.")
	createUniverseCmd.Flags().StringArray("storage-type", []string{},
		"[Optional] Storage type (EBS for AWS) used for this instance. Provide the storage type "+
			" of volumes for each cluster as a separate flag. "+
			"Defaults to \"GP3\" for aws, \"Premium_LRS\" for azure and \"Persistent\" for gcp.")
	createUniverseCmd.Flags().StringArray("storage-class", []string{},
		"[Optional] Name of the storage class, supported for Kubernetes. Provide "+
			"the storage type of volumes for each cluster as a separate flag. Defaults"+
			" to \"standard\".")
	createUniverseCmd.Flags().IntSlice("disk-iops", []int{3000, 3000},
		"[Optional] Desired IOPS for the volumes mounted on this instance,"+
			" supported only for AWS. Provide the number of "+
			"volumes for each cluster as a separate flag or as comma separated values.")
	createUniverseCmd.Flags().IntSlice("throughput", []int{125, 125},
		"[Optional] Desired throughput for the volumes mounted on this instance in MB/s, "+
			"supported only for AWS. Provide throughput "+
			"for each cluster as a separate flag or as comma separated values.")
	createUniverseCmd.Flags().Float64Slice("k8s-tserver-mem-size", []float64{4, 4},
		"[Optional] Memory size of the kubernetes tserver node in GB. Provide k8s-tserver-mem-size "+
			"for each cluster as a separate flag or as comma separated values.")
	createUniverseCmd.Flags().Float64Slice("k8s-tserver-cpu-core-count", []float64{2, 2},
		"[Optional] CPU core count of the kubernetes tserver node. Provide k8s-tserver-cpu-core-count "+
			"for each cluster as a separate flag or as comma separated values.")

	// if dedicated nodes is set to true
	createUniverseCmd.Flags().String("dedicated-master-instance-type", "",
		"[Optional] Instance Type for the dedicated master nodes in the primary cluster."+
			" Defaults to \"c5.large\" for aws, \"Standard_DS2_v2\" "+
			"for azure and \"n1-standard-1\" for gcp. Fetches the first available "+
			"instance type for onprem providers.")
	createUniverseCmd.Flags().Int("dedicated-master-num-volumes", 1,
		"[Optional] Number of volumes to be mounted on master instance at the default path.")
	createUniverseCmd.Flags().Int("dedicated-master-volume-size", 100,
		"[Optional] The size of each volume in each master instance.")
	// Comma separated values in a single string
	createUniverseCmd.Flags().String("dedicated-master-mount-points", "",
		"[Optional] Disk mount points for master nodes. Provide comma-separated strings "+
			"in the following format: \"--mount-points 'mount-point-1-for-master,"+
			"mount-point-2-for-master'\""+
			" Defaults to null for aws, azure, gcp. Fetches the first available "+
			"instance mount points for onprem providers.")
	createUniverseCmd.Flags().String("dedicated-master-storage-type", "",
		"[Optional] Storage type (EBS for AWS) used for master instance. "+
			"Defaults to \"GP3\" for aws, \"Premium_LRS\" for azure and \"Persistent\" for gcp. "+
			"Fetches the first available storage type for onprem providers.")
	createUniverseCmd.Flags().String("dedicated-master-storage-class", "",
		"[Optional] Name of the storage class for the master instance. "+
			"Defaults to \"standard\".")
	createUniverseCmd.Flags().Int("dedicated-master-disk-iops", 3000,
		"[Optional] Desired IOPS for the volumes mounted on this instance,"+
			" supported only for AWS.")
	createUniverseCmd.Flags().Int("dedicated-master-throughput", 125,
		"[Optional] Desired throughput for the volumes mounted on this instance in MB/s, "+
			"supported only for AWS.")
	createUniverseCmd.Flags().Float64Slice("k8s-master-mem-size", []float64{4, 4},
		"[Optional] Memory size of the kubernetes master node in GB. Provide k8s-tserver-mem-size "+
			"for each cluster as a separate flag or as comma separated values.")
	createUniverseCmd.Flags().Float64Slice("k8s-master-cpu-core-count", []float64{2, 2},
		"[Optional] CPU core count of the kubernetes master node. Provide k8s-tserver-cpu-core-count "+
			"for each cluster as a separate flag or as comma separated values.")

	// Advanced configuration // taken only for Primary cluster
	createUniverseCmd.Flags().Bool("assign-public-ip", true,
		"[Optional] Assign Public IPs to the DB servers for connections over the internet.")
	createUniverseCmd.Flags().Bool("assign-static-public-ip", true,
		"[Optional] Assign Static Public IPs to the DB servers for connections over the internet.")
	createUniverseCmd.Flags().Bool("enable-ysql", true,
		"[Optional] Enable YSQL endpoint.")
	createUniverseCmd.Flags().String("ysql-password", "",
		"[Optional] YSQL authentication password. Use single quotes ('') to provide "+
			"values with special characters.")
	createUniverseCmd.Flags().Bool("enable-ycql", true,
		"[Optional] Enable YCQL endpoint.")
	createUniverseCmd.Flags().String("ycql-password", "",
		"[Optional] YCQL authentication password. Use single quotes ('') to provide "+
			"values with special characters.")
	createUniverseCmd.Flags().Bool("enable-yedis", false,
		"[Optional] Enable YEDIS endpoint. (default false)")

	// Encryption fields

	createUniverseCmd.Flags().Bool("enable-node-to-node-encrypt", true,
		"[Optional] Enable Node-to-Node encryption to use TLS enabled connections for "+
			"communication between different Universe nodes.")
	createUniverseCmd.Flags().Bool("enable-client-to-node-encrypt", true,
		"[Optional] Enable Client-to-Node encryption to use TLS enabled connection for "+
			"communication between a client (ex: Database application, ysqlsh, ycqlsh) "+
			"and the Universe YSQL -or- YCQL endpoint.")
	createUniverseCmd.Flags().String("root-ca", "",
		"[Optional] Root Certificate name for Encryption in Transit, defaults to creating new"+
			" certificate for the universe if encryption in transit in enabled.")

	createUniverseCmd.Flags().Bool("enable-volume-encryption", false,
		"[Optional] Enable encryption for data stored on the tablet servers. (default false)")
	createUniverseCmd.Flags().String("kms-config", "",
		"[Optional] Key management service config name. "+
			formatter.Colorize("Required when enable-volume-encryption is set to true.",
				formatter.GreenColor))

	createUniverseCmd.Flags().Bool("enable-ipv6", false,
		"[Optional] Enable IPV6 networking for connections between the DB Servers, supported "+
			"only for Kubernetes universes (default false) ")
	createUniverseCmd.Flags().String("yb-db-version", "",
		"[Optional] YugabyteDB Software Version, defaults to the latest available version. "+
			"Run \"yba yb-db-version list\" to find the latest version.")
	createUniverseCmd.Flags().Bool("use-systemd", true,
		"[Optional] Use SystemD.")
	createUniverseCmd.Flags().String("access-key-code", "",
		"[Optional] Access Key code (UUID) corresponding to the provider,"+
			" defaults to the provider's access key.")
	createUniverseCmd.Flags().String("aws-arn-string", "", "[Optional] Instance Profile "+
		"ARN for AWS universes.")

	createUniverseCmd.Flags().StringToString("user-tags",
		map[string]string{}, "[Optional] User Tags for the DB instances. Provide "+
			"as key=value pairs per flag. Example \"--user-tags "+
			"name=test --user-tags owner=development\" OR "+
			"\"--user-tags name=test,owner=development\".")
	createUniverseCmd.Flags().String("kubernetes-universe-overrides-file-path", "",
		"[Optional] Helm Overrides file path for the universe, supported for Kubernetes."+
			" For examples on universe overrides file contents, please refer to: "+
			"\"https://docs.yugabyte.com/stable/yugabyte-platform/"+
			"create-deployments/create-universe-multi-zone-kubernetes/#configure-helm-overrides\"")
	createUniverseCmd.Flags().StringArray("kubernetes-az-overrides-file-path", []string{},
		"[Optional] Helm Overrides file paths for the availabilty zone, supported for Kubernetes."+
			" Provide file paths for overrides of each Availabilty zone as a separate flag."+
			" For examples on availabilty zone overrides file contents, please refer to: "+
			"\"https://docs.yugabyte.com/stable/yugabyte-platform/"+
			"create-deployments/create-universe-multi-zone-kubernetes/#configure-helm-overrides\"")

	// Inputs for communication ports

	createUniverseCmd.Flags().Int("master-http-port", 7000,
		"[Optional] Master HTTP Port.")
	createUniverseCmd.Flags().Int("master-rpc-port", 7100,
		"[Optional] Master RPC Port.")
	createUniverseCmd.Flags().Int("node-exporter-port", 9300,
		"[Optional] Node Exporter Port.")
	createUniverseCmd.Flags().Int("redis-server-http-port", 11000,
		"[Optional] Redis Server HTTP Port.")
	createUniverseCmd.Flags().Int("redis-server-rpc-port", 6379,
		"[Optional] Redis Server RPC Port.")
	createUniverseCmd.Flags().Int("tserver-http-port", 9000,
		"[Optional] TServer HTTP Port.")
	createUniverseCmd.Flags().Int("tserver-rpc-port", 9100,
		"[Optional] TServer RPC Port.")
	createUniverseCmd.Flags().Int("yql-server-http-port", 12000,
		"[Optional] YQL Server HTTP Port.")
	createUniverseCmd.Flags().Int("yql-server-rpc-port", 9042,
		"[Optional] YQL Server RPC Port.")
	createUniverseCmd.Flags().Int("ysql-server-http-port", 13000,
		"[Optional] YSQL Server HTTP Port.")
	createUniverseCmd.Flags().Int("ysql-server-rpc-port", 5433,
		"[Optional] YSQL Server RPC Port.")

	v1.BindPFlag("name", createUniverseCmd.Flags().Lookup("name"))
	v1.BindPFlag("provider-code", createUniverseCmd.Flags().Lookup("provider-code"))
	v1.BindPFlag("provider-name", createUniverseCmd.Flags().Lookup("provider-name"))
	v1.BindPFlag("dedicated-nodes", createUniverseCmd.Flags().Lookup("dedicated-nodes"))
	v1.BindPFlag("add-read-replica", createUniverseCmd.Flags().Lookup("add-read-replica"))
	v1.BindPFlag("replication-factor", createUniverseCmd.Flags().Lookup("replication-factor"))
	v1.BindPFlag("num-nodes", createUniverseCmd.Flags().Lookup("num-nodes"))
	v1.BindPFlag("regions", createUniverseCmd.Flags().Lookup("regions"))
	v1.BindPFlag("preferred-region", createUniverseCmd.Flags().Lookup("preferred-region"))
	v1.BindPFlag("master-gflags", createUniverseCmd.Flags().Lookup("master-gflags"))
	v1.BindPFlag("tserver-gflags", createUniverseCmd.Flags().Lookup("tserver-gflags"))
	v1.BindPFlag("instance-type", createUniverseCmd.Flags().Lookup("instance-type"))
	v1.BindPFlag("num-volumes", createUniverseCmd.Flags().Lookup("num-volumes"))
	v1.BindPFlag("volume-size", createUniverseCmd.Flags().Lookup("volume-size"))
	v1.BindPFlag("mount-points", createUniverseCmd.Flags().Lookup("mount-points"))
	v1.BindPFlag("storage-type", createUniverseCmd.Flags().Lookup("storage-type"))
	v1.BindPFlag("storage-class", createUniverseCmd.Flags().Lookup("storage-class"))
	v1.BindPFlag("disk-iops", createUniverseCmd.Flags().Lookup("disk-iops"))
	v1.BindPFlag("throughput", createUniverseCmd.Flags().Lookup("throughput"))
	v1.BindPFlag("k8s-tserver-mem-size", createUniverseCmd.Flags().Lookup("k8s-tserver-mem-size"))
	v1.BindPFlag("k8s-tserver-cpu-core-count", createUniverseCmd.Flags().Lookup("k8s-tserver-cpu-core-count"))
	v1.BindPFlag("dedicated-master-instance-type", createUniverseCmd.Flags().Lookup("dedicated-master-instance-type"))
	v1.BindPFlag("dedicated-master-num-volumes", createUniverseCmd.Flags().Lookup("dedicated-master-num-volumes"))
	v1.BindPFlag("dedicated-master-volume-size", createUniverseCmd.Flags().Lookup("dedicated-master-volume-size"))
	v1.BindPFlag("dedicated-master-mount-points", createUniverseCmd.Flags().Lookup("dedicated-master-mount-points"))
	v1.BindPFlag("dedicated-master-storage-type", createUniverseCmd.Flags().Lookup("dedicated-master-storage-type"))
	v1.BindPFlag("dedicated-master-storage-class", createUniverseCmd.Flags().Lookup("dedicated-master-storage-class"))
	v1.BindPFlag("dedicated-master-disk-iops", createUniverseCmd.Flags().Lookup("dedicated-master-disk-iops"))
	v1.BindPFlag("dedicated-master-throughput", createUniverseCmd.Flags().Lookup("dedicated-master-throughput"))
	v1.BindPFlag("k8s-master-mem-size", createUniverseCmd.Flags().Lookup("k8s-master-mem-size"))
	v1.BindPFlag("k8s-master-cpu-core-count", createUniverseCmd.Flags().Lookup("k8s-master-cpu-core-count"))
	v1.BindPFlag("assign-public-ip", createUniverseCmd.Flags().Lookup("assign-public-ip"))
	v1.BindPFlag("enable-ysql", createUniverseCmd.Flags().Lookup("enable-ysql"))
	v1.BindPFlag("ysql-password", createUniverseCmd.Flags().Lookup("ysql-password"))
	v1.BindPFlag("enable-ycql", createUniverseCmd.Flags().Lookup("enable-ycql"))
	v1.BindPFlag("ycql-password", createUniverseCmd.Flags().Lookup("ycql-password"))
	v1.BindPFlag("enable-yedis", createUniverseCmd.Flags().Lookup("enable-yedis"))
	v1.BindPFlag("enable-node-to-node-encrypt", createUniverseCmd.Flags().Lookup("enable-node-to-node-encrypt"))
	v1.BindPFlag("enable-client-to-node-encrypt", createUniverseCmd.Flags().Lookup("enable-client-to-node-encrypt"))
	v1.BindPFlag("root-ca", createUniverseCmd.Flags().Lookup("root-ca"))
	v1.BindPFlag("enable-volume-encryption", createUniverseCmd.Flags().Lookup("enable-volume-encryption"))
	v1.BindPFlag("kms-config", createUniverseCmd.Flags().Lookup("kms-config"))
	v1.BindPFlag("enable-ipv6", createUniverseCmd.Flags().Lookup("enable-ipv6"))
	v1.BindPFlag("yb-db-version", createUniverseCmd.Flags().Lookup("yb-db-version"))
	v1.BindPFlag("use-systemd", createUniverseCmd.Flags().Lookup("use-systemd"))
	v1.BindPFlag("access-key-code", createUniverseCmd.Flags().Lookup("access-key-code"))
	v1.BindPFlag("aws-arn-string", createUniverseCmd.Flags().Lookup("aws-arn-string"))
	v1.BindPFlag("user-tags", createUniverseCmd.Flags().Lookup("user-tags"))
	v1.BindPFlag("kubernetes-universe-overrides-file-path", createUniverseCmd.Flags().Lookup("kubernetes-universe-overrides-file-path"))
	v1.BindPFlag("kubernetes-az-overrides-file-path", createUniverseCmd.Flags().Lookup("kubernetes-az-overrides-file-path"))
	v1.BindPFlag("master-http-port", createUniverseCmd.Flags().Lookup("master-http-port"))
	v1.BindPFlag("master-rpc-port", createUniverseCmd.Flags().Lookup("master-rpc-port"))
	v1.BindPFlag("node-exporter-port", createUniverseCmd.Flags().Lookup("node-exporter-port"))
	v1.BindPFlag("redis-server-http-port", createUniverseCmd.Flags().Lookup("redis-server-http-port"))
	v1.BindPFlag("redis-server-rpc-port", createUniverseCmd.Flags().Lookup("redis-server-rpc-port"))
	v1.BindPFlag("tserver-http-port", createUniverseCmd.Flags().Lookup("tserver-http-port"))
	v1.BindPFlag("tserver-rpc-port", createUniverseCmd.Flags().Lookup("tserver-rpc-port"))
	v1.BindPFlag("yql-server-http-port", createUniverseCmd.Flags().Lookup("yql-server-http-port"))
	v1.BindPFlag("yql-server-rpc-port", createUniverseCmd.Flags().Lookup("yql-server-rpc-port"))
	v1.BindPFlag("ysql-server-http-port", createUniverseCmd.Flags().Lookup("ysql-server-http-port"))
	v1.BindPFlag("ysql-server-rpc-port", createUniverseCmd.Flags().Lookup("ysql-server-rpc-port"))

}

func initializeViper(config string) {
	var err error
	if len(strings.TrimSpace(config)) != 0 {
		v1.SetConfigFile(config)
		err = v1.ReadInConfig()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		logrus.Info("Creating universe using config file: " + config + "\n")
	}
}
