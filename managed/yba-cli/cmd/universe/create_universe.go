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

// createUniverseCmd represents the universe command
var createUniverseCmd = &cobra.Command{
	Use:   "create [universe-name]",
	Short: "Create an YugabyteDB Anywhere universe",
	Long:  "Create an universe in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !(len(args) > 0 || len(universeNameFlag) > 0) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to create\n", formatter.RedColor))
		}
		enableVolumeEncryption, err := cmd.Flags().GetBool("enable-volume-encryption")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if enableVolumeEncryption {
			cmd.MarkFlagRequired("kms-config")
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		var response *http.Response
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		universeNameFlag, _ := cmd.Flags().GetString("name")
		var universeName string
		if len(args) > 0 {
			universeName = args[0]
		} else if len(universeNameFlag) > 0 {
			universeName = universeNameFlag
		}

		allowed, version, err := authAPI.UniverseYBAVersionCheck()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !allowed {
			logrus.Fatalf(fmt.Sprintf("Creating universes below version %s (or on restricted"+
				" versions) is not supported, currently on %s", util.YBAAllowUniverseMinVersion,
				version))
		}

		enableYbc := true
		communicationPorts, err := buildCommunicationPorts(cmd)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		certUUID := ""
		clientRootCA, err := cmd.Flags().GetString("root-ca")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		// find the root certficate UUID from the name
		if len(clientRootCA) != 0 {
			certUUID, response, err = authAPI.GetCertificate(clientRootCA).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Create")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
		}

		kmsConfigUUID := ""
		var opType string
		enableVolumeEncryption, err := cmd.Flags().GetBool("enable-volume-encryption")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if enableVolumeEncryption {
			opType = "ENABLE"
			kmsConfigName, err := cmd.Flags().GetString("kms-config")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			// find kmsConfigUUID from the name
			kmsConfigs, response, err := authAPI.ListKMSConfigs().Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Create")
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
						}
					}
				}
			}
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

		msg := fmt.Sprintf("The universe %s is being created",
			formatter.Colorize(universeName, formatter.GreenColor))

		if viper.GetBool("wait") {
			if taskUUID != "" {
				logrus.Info(fmt.Sprintf("\nWaiting for universe %s (%s) to be created\n",
					formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
				err = authAPI.WaitForTask(taskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			fmt.Printf("The universe %s (%s) has been created\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

			universeData, response, err = authAPI.ListUniverses().Name(universeName).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Create - Fetch Universe")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			universesCtx := formatter.Context{
				Output: os.Stdout,
				Format: universe.NewUniverseFormat(viper.GetString("output")),
			}

			universe.Write(universesCtx, universeData)

		} else {
			fmt.Println(msg)
		}

	},
}

func init() {

	createUniverseCmd.Flags().SortFlags = false

	createUniverseCmd.Flags().StringP("name", "n", "",
		"[Optional] The name of the universe to be created.")

	createUniverseCmd.Flags().String("provider-code", "",
		"[Required] Provider code. Allowed values: aws, gcp, azu, onprem, kubernetes.")

	setDefaults()

	// fss := cliflag.NamedFlagSets{}
	// createUniverseCmd.AddGroup()

	createUniverseCmd.Flags().String("provider-name", "",
		"[Optional] Provider name to be used in universe. "+
			"Run \"yba provider list --code <provider-code>\" "+
			"to check the default provider for the given provider-code.")
	createUniverseCmd.MarkFlagRequired("provider-code")
	createUniverseCmd.Flags().Bool("dedicated-nodes", false,
		"[Optional] Place Masters on dedicated nodes, defaults to false for aws, azu, gcp, onprem."+
			" Defaults to true for kubernetes.")
	createUniverseCmd.Flags().Bool("add-read-replica", false,
		"[Optional] Add a read replica cluster to the universe, defaults to false.")

	// Following fields are required individually for both primary and read replica cluster

	createUniverseCmd.Flags().IntSlice("replication-factor", []int{3, 3},
		"[Optional] Replication factor of the cluster. Provide replication-factor for each "+
			"cluster as a separate flag. \"--replication-factor 3 --replication-factor 5\" OR "+
			"\"--replication-factor 3,5\" refers to RF "+
			"of Primary cluster = 3 and RF of Read Replica = 5. First flag always corresponds to"+
			" the primary cluster. Defaults to 3 for both clusters.")
	createUniverseCmd.Flags().IntSlice("num-nodes", []int{3, 3},
		"[Optional] Number of nodes in the cluster. Provide no of nodes for each cluster "+
			"as a separate flag. \"--num-nodes 3 --num-nodes 5\" "+
			"OR \"--num-nodes 3,5\" "+
			"refers to 3 nodes in the Primary cluster and 3 nodes in the Read Replica cluster"+
			". First flag always corresponds to"+
			" the primry cluster. Defaults to 3 for both clusters.")
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
		"[Optional] Master GFlags. Provide comma-separated key-value pairs for the primary "+
			"cluster in the following format: "+
			"\"--master-gflags master-gflag-key-1=master-gflag-value-1,"+
			"master-gflag-key-2=master-gflag-key2\".")

	createUniverseCmd.Flags().StringArray("tserver-gflags", []string{},
		"[Optional] TServer GFlags. Provide comma-separated key-value pairs for each "+
			"cluster as a separate flag in the following format: "+
			"\"--tserver-gflags tserver-gflag-key-1-for-primary-cluster=tserver-gflag-value-1,"+
			"tserver-gflag-key-2-for-primary-cluster=tserver-gflag-key2 "+
			"--tserver-gflags tserver-gflag-key-1-for-read-replica=tserver-gflag-value-1,"+
			"tserver-gflag-key-2-for-read-replica=tserver-gflag-key2\". If no-of-clusters = 2 "+
			"and no tserver gflags are provided for the read replica, the primary cluster gflags are "+
			"by default applied to the read replica cluster.")

	// Device Info per cluster
	createUniverseCmd.Flags().StringArray("instance-type", []string{},
		"[Optional] Instance Type for the universe nodes. Provide the instance types for each "+
			"cluster as a separate flag."+
			" Defaults to \"c5.large\" for aws, \"Standard_DS2_v2\" "+
			"for azure and \"n1-standard-1\" for gcp. Fetches the first available "+
			"instance type for onprem providers.")
	createUniverseCmd.Flags().IntSlice("num-volumes", []int{1, 1},
		"[Optional] Number of volumes to be mounted on this instance at the default path."+
			" Provide the number of volumes for each "+
			"cluster as a separate flag or as comma separated values. Defaults to 1 per node.")
	createUniverseCmd.Flags().IntSlice("volume-size", []int{},
		"[Optional] The size of each volume in each instance. Provide the number of "+
			"volumes for each cluster as a separate flag or as comma separated values."+
			" Defaults to 100 GB per node.")
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
	createUniverseCmd.Flags().IntSlice("disk-iops", []int{},
		"[Optional] Desired IOPS for the volumes mounted on this instance,"+
			" supported only for AWS. Provide the number of "+
			"volumes for each cluster as a separate flag or as comma separated values. "+
			"Defaults to 3000.")
	createUniverseCmd.Flags().IntSlice("throughput", []int{},
		"[Optional] Desired throughput for the volumes mounted on this instance in MB/s, "+
			"supported only for AWS. Provide throughput "+
			"for each cluster as a separate flag or as comma separated values. "+
			"Defaults to 125.")

	// if dedicated nodes is set to true
	createUniverseCmd.Flags().String("dedicated-master-instance-type", "",
		"[Optional] Instance Type for the dedicated master nodes in the primary cluster."+
			" Defaults to \"c5.large\" for aws, \"Standard_DS2_v2\" "+
			"for azure and \"n1-standard-1\" for gcp. Fetches the first available "+
			"instance type for onprem providers.")
	createUniverseCmd.Flags().Int("dedicated-master-num-volumes", 1,
		"[Optional] Number of volumes to be mounted on master instance at the default path."+
			" Defaults to 1 per node.")
	createUniverseCmd.Flags().Int("dedicated-master-volume-size", 100,
		"[Optional] The size of each volume in each master instance."+
			" Defaults to 100 GB per node.")
	// Comma separated values in a single string
	createUniverseCmd.Flags().String("dedicated-master-mount-points", "",
		"[Optional] Disk mount points for master nodes. Provide comma-separated strings "+
			"in the following format: \"--mount-points 'mount-point-1-for-master,"+
			"mount-point-2-for-master'\""+
			"Defaults to null for aws, azure, gcp. Fetches the first available "+
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
			" supported only for AWS. "+
			"Defaults to 3000.")
	createUniverseCmd.Flags().Int("dedicated-master-throughput", 125,
		"[Optional] Desired throughput for the volumes mounted on this instance in MB/s, "+
			"supported only for AWS. Defaults to 125.")

	// Advanced configuratopn // taken only for Primary cluster
	createUniverseCmd.Flags().Bool("assign-public-ip", true,
		"[Optional] Assign Public IPs to the DB servers for connections over the internet.")
	createUniverseCmd.Flags().Bool("enable-ysql", true,
		"[Optional] Enable YSQL endpoint, defaults to true.")
	createUniverseCmd.Flags().String("ysql-password", "",
		"[Optional] YSQL authentication password.")
	createUniverseCmd.Flags().Bool("enable-ycql", true,
		"[Optional] Enable YCQL endpoint, defaults to true.")
	createUniverseCmd.Flags().String("ycql-password", "",
		"[Optional] YCQL authentication password.")
	createUniverseCmd.Flags().Bool("enable-yedis", false,
		"[Optional] Enable YEDIS endpoint, defaults to false.")

	// Encryption fields

	createUniverseCmd.Flags().Bool("enable-node-to-node-encrypt", true,
		"[Optional] Enable Node-to-Node encryption to use TLS enabled connections for "+
			"communication between different Universe nodes, defaults to true.")
	createUniverseCmd.Flags().Bool("enable-client-to-node-encrypt", true,
		"[Optional] Enable Client-to-Node encryption to use TLS enabled connection for "+
			"communication between a client (ex: Database application, ysqlsh, ycqlsh) "+
			"and the Universe YSQL -or- YCQL endpoint, defaults to true.")
	createUniverseCmd.Flags().String("root-ca", "",
		"[Optional] Root Certificate name for Encryption in Transit, defaults to creating new"+
			" certificate for the universe if encryption in transit in enabled.")

	createUniverseCmd.Flags().Bool("enable-volume-encryption", false,
		"[Optional] Enable encryption for data stored on the tablet servers, defaults to false.")
	createUniverseCmd.Flags().String("kms-config", "",
		"[Optional] Key management service config. "+
			formatter.Colorize("Required when enable-volume-encryption is set to true.",
				formatter.GreenColor))

	createUniverseCmd.Flags().Bool("enable-ipv6", false,
		"[Optional] Enable IPV6 networking for connections between the DB Servers, supported "+
			"only for Kubernetes universes defaults to false. ")
	createUniverseCmd.Flags().String("yb-db-version", "",
		"[Optional] YugabyteDB Software Version, defaults to the latest available version"+
			"Run \"yba release list\" to find the latest version.")
	createUniverseCmd.Flags().Bool("use-systemd", true,
		"[Optional] Use SystemD, defaults to true.")
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

	// Inputs for communication ports

	createUniverseCmd.Flags().Int("master-http-port", 7000,
		"[Optional] Master HTTP Port, defaults to 7000.")
	createUniverseCmd.Flags().Int("master-rpc-port", 7100,
		"[Optional] Master RPC Port, defaults to 7100.")
	createUniverseCmd.Flags().Int("node-exporter-port", 9300,
		"[Optional] Node Exporter Port, defaults to 9300.")
	createUniverseCmd.Flags().Int("redis-server-http-port", 11000,
		"[Optional] Redis Server HTTP Port, defaults to 11000.")
	createUniverseCmd.Flags().Int("redis-server-rpc-port", 6379,
		"[Optional] Redis Server RPC Port, defaults to 6379.")
	createUniverseCmd.Flags().Int("tserver-http-port", 9000,
		"[Optional] TServer HTTP Port, defaults to 9000.")
	createUniverseCmd.Flags().Int("tserver-rpc-port", 9100,
		"[Optional] TServer RPC Port, defaults to 9100.")
	createUniverseCmd.Flags().Int("yql-server-http-port", 12000,
		"[Optional] YQL Server HTTP Port, defaults to 12000.")
	createUniverseCmd.Flags().Int("yql-server-rpc-port", 9042,
		"[Optional] YQL Server RPC Port, defaults to 9042.")
	createUniverseCmd.Flags().Int("ysql-server-http-port", 13000,
		"[Optional] YSQL Server HTTP Port, defaults to 13000.")
	createUniverseCmd.Flags().Int("ysql-server-rpc-port", 5433,
		"[Optional] YSQL Server RPC Port, defaults to 5433.")

}

func setDefaults() {
	viper.SetDefault("dedicated-nodes", false)
	viper.SetDefault("use-systemd", true)
	viper.SetDefault("enable-ysql", true)
	viper.SetDefault("enable-ycql", true)
	viper.SetDefault("enable-volume-encryption", false)
	viper.SetDefault("enable-node-to-node-encrypt", true)
	viper.SetDefault("enable-client-to-node-encrypt", true)
}
