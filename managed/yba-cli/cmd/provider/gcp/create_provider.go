/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

import (
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createGCPProviderCmd represents the provider command
var createGCPProviderCmd = &cobra.Command{
	Use:   "create",
	Short: "Create a GCP YugabyteDB Anywhere provider",
	Long:  "Create a GCP provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !(len(providerNameFlag) > 0) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to create\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		allowed, version, err := authAPI.NewProviderYBAVersionCheck()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerCode := util.GCPProviderType
		config, err := buildGCPConfig(cmd)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		airgapInstall, err := cmd.Flags().GetBool("airgap-install")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sshUser, err := cmd.Flags().GetString("ssh-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sshPort, err := cmd.Flags().GetInt("ssh-port")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		keyPairName, err := cmd.Flags().GetString("custom-ssh-keypair-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		network, err := cmd.Flags().GetString("network")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		filePath, err := cmd.Flags().GetString("custom-ssh-keypair-file-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var sshFileContent string
		if len(filePath) > 0 {
			sshFileContentByte, err := os.ReadFile(filePath)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			sshFileContent = string(sshFileContentByte)
		}

		regions, err := cmd.Flags().GetStringArray("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.Provider{
			Code:                 util.GetStringPointer(providerCode),
			Config:               util.StringMap(config),
			DestVpcId:            util.GetStringPointer(network),
			Name:                 util.GetStringPointer(providerName),
			AirGapInstall:        util.GetBoolPointer(airgapInstall),
			SshPort:              util.GetInt32Pointer(int32(sshPort)),
			SshUser:              util.GetStringPointer(sshUser),
			KeyPairName:          util.GetStringPointer(keyPairName),
			SshPrivateKeyContent: util.GetStringPointer(sshFileContent),
			Regions:              buildGCPRegions(regions, allowed, version),
		}
		rCreate, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider: GCP", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rCreate.GetResourceUUID()
		taskUUID := rCreate.GetTaskUUID()

		providerutil.WaitForCreateProviderTask(authAPI, providerName, providerUUID,
			providerCode, taskUUID)
	},
}

func init() {
	createGCPProviderCmd.Flags().SortFlags = false

	// Flags needed for GCP
	createGCPProviderCmd.Flags().String("credentials", "",
		fmt.Sprintf("GCP Service Account credentials file path. "+
			"Can also be set using environment variable %s.", util.GCPCredentialsEnv))

	createGCPProviderCmd.Flags().String("network", "",
		"[Required] Custom GCE network name.")
	createGCPProviderCmd.MarkFlagRequired("network")
	createGCPProviderCmd.Flags().String("yb-firewall-tags", "",
		"[Optional] Tags for firewall rules in GCP.")
	createGCPProviderCmd.Flags().Bool("use-host-vpc", true,
		"[Optional] Enabling YugabyteDB Anywhere Host VPC in GCP.")
	createGCPProviderCmd.Flags().Bool("use-host-credentials", false,
		"[Optional] Enabling YugabyteDB Anywhere Host credentials in GCP, defaults to false.")
	createGCPProviderCmd.Flags().String("project-id", "",
		"[Optional] Project ID that hosts universe nodes in GCP.")

	createGCPProviderCmd.Flags().StringArray("region", []string{},
		"[Required] Region associated with the GCP provider. Minimum number of required "+
			"regions = 1. Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,shared-subnet=<subnet-id>,yb-image=<custom-ami>,"+
			"instance-template=<instance-templates-for-YugabyteDB-nodes>\". "+
			formatter.Colorize("Region name and Shared subnet are required key-value pairs.",
				formatter.GreenColor)+
			" YB Image (AMI) and Instance Template (accepted in YugabyteDB Anywhere versions "+
			">= 2.18.0) are optional. "+
			"Each region can be added using separate --region flags. "+
			"Example: --region region-name=us-west1,shared-subnet=<shared-subnet-id>")

	createGCPProviderCmd.Flags().String("ssh-user", "",
		"[Optional] SSH User to access the YugabyteDB nodes.")
	createGCPProviderCmd.Flags().Int("ssh-port", 22,
		"[Optional] SSH Port to access the YugabyteDB nodes.")
	createGCPProviderCmd.Flags().String("custom-ssh-keypair-name", "",
		"[Optional] Provide custom key pair name to access YugabyteDB nodes. "+
			"YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.")
	createGCPProviderCmd.Flags().String("custom-ssh-keypair-file-path", "",
		"[Optional] Provide custom key pair file path to access YugabyteDB nodes. "+
			formatter.Colorize("Required with --custom-ssh-keypair-name.",
				formatter.GreenColor))
	createGCPProviderCmd.MarkFlagsRequiredTogether("custom-ssh-keypair-name",
		"custom-ssh-keypair-file-path")

	createGCPProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads, "+
			"defaults to false.")
}

func buildGCPConfig(cmd *cobra.Command) (map[string]interface{}, error) {

	var err error
	config := make(map[string]interface{})

	useHostCredentials, err := cmd.Flags().GetBool("use-host-credentials")
	if err != nil {
		return nil, err
	}

	if !useHostCredentials {
		gcpCredFilePath, err := cmd.Flags().GetString("credentials")
		if err != nil {
			return nil, err
		}
		if len(gcpCredFilePath) == 0 {
			config, err = util.GcpGetCredentialsAsMap()
			if err != nil {
				return nil, err
			}
		} else {
			config, err = util.GcpGetCredentialsAsMapFromFilePath(gcpCredFilePath)
			if err != nil {
				return nil, err
			}
		}
	}

	config["use_host_credentials"] = strconv.FormatBool(useHostCredentials)

	ybFirewallTags, err := cmd.Flags().GetString("yb-firewall-tags")
	if err != nil {
		return nil, err
	}
	if len(ybFirewallTags) > 0 {
		config["YB_FIREWALL_TAGS"] = ybFirewallTags
	}
	useHostVpc, err := cmd.Flags().GetBool("use-host-vpc")
	if err != nil {
		return nil, err
	}
	config["use_host_vpc"] = strconv.FormatBool(useHostVpc)

	projectID, err := cmd.Flags().GetString("project-id")
	if err != nil {
		return nil, err
	}
	if len(projectID) > 0 {
		config["project_id"] = projectID
	}
	network, err := cmd.Flags().GetString("network")
	if err != nil {
		return nil, err
	}
	if len(network) > 0 {
		config["network"] = network
	}

	return config, nil
}

func buildGCPRegions(regionStrings []string, allowed bool, version string) (
	res []ybaclient.Region) {
	if len(regionStrings) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one region is required per provider.",
				formatter.RedColor))
	}
	for _, regionString := range regionStrings {
		region := map[string]string{}
		for _, regionInfo := range strings.Split(regionString, ",") {
			kvp := strings.Split(regionInfo, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in region description.",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "region-name":
				if len(strings.TrimSpace(val)) != 0 {
					region["name"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "shared-subnet":
				if len(strings.TrimSpace(val)) != 0 {
					region["shared-subnet"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "instance-template":
				if len(strings.TrimSpace(val)) != 0 {
					region["instance-template"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "yb-image":
				if len(strings.TrimSpace(val)) != 0 {
					region["yb-image"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			}
		}
		if _, ok := region["name"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Name not specified in region.",
					formatter.RedColor))
		}
		if _, ok := region["shared-subnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Subnet ID not specified in region.",
					formatter.RedColor))
		}

		zones := buildGCPZones(region["shared-subnet"])
		r := ybaclient.Region{
			Code:    util.GetStringPointer(region["name"]),
			Name:    util.GetStringPointer(region["name"]),
			YbImage: util.GetStringPointer(region["yb-image"]),
			Zones:   zones,
		}
		if allowed {
			r.Details = &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Gcp: &ybaclient.GCPRegionCloudInfo{
						YbImage:          util.GetStringPointer(region["yb-image"]),
						InstanceTemplate: util.GetStringPointer(region["instance-template"]),
					},
				},
			}
		} else {
			logrus.Info(
				fmt.Sprintf("YugabyteDB Anywhere version %s does not support Instance "+
					"Templates, ignoring value.", version))
		}
		res = append(res, r)
	}
	return res
}

func buildGCPZones(sharedSubnet string) (res []ybaclient.AvailabilityZone) {
	z := ybaclient.AvailabilityZone{
		Subnet: util.GetStringPointer(sharedSubnet),
	}
	res = append(res, z)
	if len(res) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.",
				formatter.RedColor))
	}
	return res
}
