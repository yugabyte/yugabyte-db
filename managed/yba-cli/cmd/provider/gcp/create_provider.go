/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcp

import (
	"fmt"
	"os"
	"strconv"

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
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a GCP YugabyteDB Anywhere provider",
	Long:    "Create a GCP provider in YugabyteDB Anywhere",
	Example: `yba provider gcp create -n dkumar-cli \
	--network yugabyte-network \
	--region region-name=us-west1::shared-subnet=<subnet> \
	--region region-name=us-west2::shared-subnet=<subnet> \
	--credentials <path-to-credentials-file>`,
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

		var gcpCloudInfo ybaclient.GCPCloudInfo

		useHostCredentials, err := cmd.Flags().GetBool("use-host-credentials")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !useHostCredentials {
			credentialsFilePath, err := cmd.Flags().GetString("credentials")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			var gcpCreds string
			if len(credentialsFilePath) == 0 {
				gcpCreds, err = util.GcpGetCredentialsAsString()
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			} else {
				gcpCreds, err = util.GcpGetCredentialsAsStringFromFilePath(credentialsFilePath)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}

			}
			gcpCloudInfo.SetGceApplicationCredentials(gcpCreds)
		} else {
			gcpCloudInfo.SetUseHostCredentials(true)
		}

		ybFirewallTags, err := cmd.Flags().GetString("yb-firewall-tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(ybFirewallTags) > 0 {
			gcpCloudInfo.SetYbFirewallTags(ybFirewallTags)
		}

		network, err := cmd.Flags().GetString("network")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		createVpc, err := cmd.Flags().GetBool("create-vpc")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if createVpc {
			gcpCloudInfo.SetUseHostVPC(false)
			if len(network) > 0 {
				gcpCloudInfo.SetDestVpcId(network)
			} else {
				errMessage := "Network required if create-vpc is set\n"
				logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
			}
		} else {
			useHostVpc, err := cmd.Flags().GetBool("use-host-vpc")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			// useHostVPC is true when we specify existing VPC and Use VPC from YBA
			// Difference is providing the network value
			// https://github.com/yugabyte/yugabyte-db/blob/bbfd0affe1f2e668dec9f1fe8cdbdea02baf8d6c/managed/src/main/java/com/yugabyte/yw/commissioner/tasks/CloudBootstrap.java#L102
			gcpCloudInfo.SetUseHostVPC(true)
			if !useHostVpc {
				if len(network) > 0 {
					gcpCloudInfo.SetDestVpcId(network)
				} else {
					errMessage := "Network required if use-host-vpc is not set\n"
					logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
				}
			}
		}

		projectID, err := cmd.Flags().GetString("project-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(projectID) > 0 {
			gcpCloudInfo.SetGceProject(projectID)
		}

		hostProjectID, err := cmd.Flags().GetString("shared-vpc-project-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(hostProjectID) > 0 {
			gcpCloudInfo.SetSharedVPCProject(hostProjectID)
		}

		airgapInstall, err := cmd.Flags().GetBool("airgap-install")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		ntpServers, err := cmd.Flags().GetStringArray("ntp-servers")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		keyPairName, err := cmd.Flags().GetString("custom-ssh-keypair-name")
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
		allAccessKeys := make([]ybaclient.AccessKey, 0)
		accessKey := ybaclient.AccessKey{
			KeyInfo: ybaclient.KeyInfo{
				KeyPairName:          util.GetStringPointer(keyPairName),
				SshPrivateKeyContent: util.GetStringPointer(sshFileContent),
			},
		}
		allAccessKeys = append(allAccessKeys, accessKey)

		regions, err := cmd.Flags().GetStringArray("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		imageBundles, err := cmd.Flags().GetStringArray("image-bundle")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.Provider{
			Code:          util.GetStringPointer(providerCode),
			Name:          util.GetStringPointer(providerName),
			ImageBundles:  buildGCPImageBundles(imageBundles),
			AllAccessKeys: allAccessKeys,
			Details: &ybaclient.ProviderDetails{
				AirGapInstall: util.GetBoolPointer(airgapInstall),
				NtpServers:    ntpServers,
				CloudInfo: &ybaclient.CloudInfo{
					Gcp: &gcpCloudInfo,
				},
			},
			Regions: buildGCPRegions(regions, allowed, version),
		}
		rTask, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Provider: GCP", "Create")
		}

		providerutil.WaitForCreateProviderTask(
			authAPI, providerName, rTask, providerCode)
	},
}

func init() {
	createGCPProviderCmd.Flags().SortFlags = false

	// Flags needed for GCP
	createGCPProviderCmd.Flags().String("credentials", "",
		fmt.Sprintf("GCP Service Account credentials file path. %s. "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required if use-host-credentials is set to false.",
				formatter.GreenColor),
			util.GCPCredentialsEnv))
	createGCPProviderCmd.Flags().Bool("use-host-credentials", false,
		"[Optional] Enabling YugabyteDB Anywhere Host credentials in GCP. (default false)")

	createGCPProviderCmd.Flags().String("network", "",
		fmt.Sprintf("[Optional] Custom GCE network name. %s",
			formatter.Colorize("Required if create-vpc is true or use-host-vpc is false.",
				formatter.GreenColor)))
	createGCPProviderCmd.Flags().String("yb-firewall-tags", "",
		"[Optional] Tags for firewall rules in GCP.")
	createGCPProviderCmd.Flags().Bool("create-vpc", false,
		"[Optional] Creating a new VPC network in GCP (Beta Feature). "+
			"Specify VPC name using --network. (default false)")
	createGCPProviderCmd.Flags().Bool("use-host-vpc", false,
		"[Optional] Using VPC from YugabyteDB Anywhere Host. "+
			"If set to false, specify an exsiting VPC using --network. "+
			"Ignored if create-vpc is set. (default false)")
	createGCPProviderCmd.Flags().String("project-id", "",
		"[Optional] Project ID that hosts universe nodes in GCP.")
	createGCPProviderCmd.Flags().String("shared-vpc-project-id", "",
		"[Optional] Shared VPC project ID in GCP. Set if you want use Shared VPC"+
			" to connect resources from multiple projects to a common VPC.")

	createGCPProviderCmd.Flags().StringArray("region", []string{},
		"[Required] Region associated with the GCP provider. Minimum number of required "+
			"regions = 1. Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"region-name=<region-name>::shared-subnet=<subnet-id>::"+
			"instance-template=<instance-templates-for-YugabyteDB-nodes>\". "+
			formatter.Colorize("Region name and Shared subnet are required key-value pairs.",
				formatter.GreenColor)+
			" YB Image (AMI) and Instance Template are optional. "+
			"Each region can be added using separate --region flags. "+
			"Example: --region region-name=us-west1::shared-subnet=<shared-subnet-id>")

	createGCPProviderCmd.Flags().StringArray("image-bundle", []string{},
		"[Optional] Intel x86_64 image bundles associated with GCP provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>::machine-image=<custom-ami>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle name, machine image and SSH user are required key-value pairs.",
				formatter.GreenColor)+
			" The default SSH Port is 22. Default marks the image bundle as default for the provider. "+
			"Each image bundle can be added using separate --image-bundle flag. "+
			"Example: --image-bundle image-bundle-name=<image-bundle>::machine-image=<custom-ami>::"+
			"ssh-user=<ssh-user>::ssh-port=22")

	createGCPProviderCmd.Flags().String("custom-ssh-keypair-name", "",
		"[Optional] Provide custom key pair name to access YugabyteDB nodes. "+
			"If left empty, "+
			"YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.")
	createGCPProviderCmd.Flags().String("custom-ssh-keypair-file-path", "",
		"[Optional] Provide custom key pair file path to access YugabyteDB nodes. "+
			formatter.Colorize("Required with --custom-ssh-keypair-name.",
				formatter.GreenColor))
	createGCPProviderCmd.MarkFlagsRequiredTogether("custom-ssh-keypair-name",
		"custom-ssh-keypair-file-path")

	createGCPProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads. "+
			"(default false)")
	createGCPProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")
}

func buildGCPRegions(regionStrings []string, allowed bool, version string) (
	res []ybaclient.Region) {
	if len(regionStrings) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one region is required per provider.\n",
				formatter.RedColor))
	}
	for _, regionString := range regionStrings {
		region := providerutil.BuildRegionMapFromString(regionString, "")
		if _, ok := region["shared-subnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Subnet ID not specified in region.\n",
					formatter.RedColor))
		}

		zones := buildGCPZones(region["shared-subnet"])
		r := ybaclient.Region{
			Code:  util.GetStringPointer(region["name"]),
			Name:  util.GetStringPointer(region["name"]),
			Zones: zones,
			Details: &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Gcp: &ybaclient.GCPRegionCloudInfo{
						InstanceTemplate: util.GetStringPointer(region["instance-template"]),
					},
				},
			},
		}
		if !allowed {
			logrus.Info(
				fmt.Sprintf("YugabyteDB Anywhere version %s does not support Instance "+
					"Templates, ignoring value.\n", version))
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
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}
	return res
}

func buildGCPImageBundles(imageBundles []string) []ybaclient.ImageBundle {
	imageBundleLen := len(imageBundles)
	res := make([]ybaclient.ImageBundle, 0)
	for _, i := range imageBundles {
		bundle := providerutil.BuildImageBundleMapFromString(i, "add")
		bundle = providerutil.DefaultImageBundleValues(bundle)

		if _, ok := bundle["ssh-user"]; !ok {
			logrus.Fatalln(
				formatter.Colorize(
					"SSH User not specified in image bundle.\n",
					formatter.RedColor))
		}

		if _, ok := bundle["machine-image"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Machine Image not specified in image bundle.\n",
					formatter.RedColor))
		}

		sshPort, err := strconv.ParseInt(bundle["ssh-port"], 10, 64)
		if err != nil {
			errMessage := err.Error() +
				" Invalid or missing value provided for 'ssh-port'. Setting it to '22'.\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			sshPort = 22
		}

		defaultBundle, err := strconv.ParseBool(bundle["default"])
		if err != nil {
			errMessage := err.Error() +
				" Invalid or missing value provided for 'default'. Setting it to 'false'.\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			defaultBundle = false
		}
		if imageBundleLen == 1 && !defaultBundle {
			defaultBundle = true
		}

		imageBundle := ybaclient.ImageBundle{
			Name:         util.GetStringPointer(bundle["name"]),
			UseAsDefault: util.GetBoolPointer(defaultBundle),
			Details: &ybaclient.ImageBundleDetails{
				Arch:          util.GetStringPointer(bundle["arch"]),
				GlobalYbImage: util.GetStringPointer(bundle["machine-image"]),
				SshUser:       util.GetStringPointer(bundle["ssh-user"]),
				SshPort:       util.GetInt32Pointer(int32(sshPort)),
			},
		}
		res = append(res, imageBundle)
	}
	return res
}
