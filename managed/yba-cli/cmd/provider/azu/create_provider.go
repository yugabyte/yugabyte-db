/*
 * Copyright (c) YugabyteDB, Inc.
 */

package azu

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

// createAzureProviderCmd represents the provider command
var createAzureProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create an Azure YugabyteDB Anywhere provider",
	Long:    "Create an Azure provider in YugabyteDB Anywhere",
	Example: `yba provider azure create -n <provider-name> \
	--region region-name=westus2::vnet=<vnet> \
	--zone zone-name=westus2-1::region-name=westus2::subnet=<subnet> \
	--rg=<az-resource-group> \
	--client-id=<az-client-id> \
	--tenant-id=<az-tenant-id> \
	--client-secret=<az-client-secret> \
	--subscription-id=<az-subscription-id>`,
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

		providerCode := util.AzureProviderType

		var azureCreds util.AzureCredentials
		var azureCloudInfo ybaclient.AzureCloudInfo

		azureCreds.ClientID, err = cmd.Flags().GetString("client-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(azureCreds.ClientID) == 0 {

			azureCreds, err = util.AzureCredentialsFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			azureCloudInfo.SetAzuClientId(azureCreds.ClientID)
			azureCloudInfo.SetAzuClientSecret(azureCreds.ClientSecret)
			azureCloudInfo.SetAzuSubscriptionId(azureCreds.SubscriptionID)
			azureCloudInfo.SetAzuTenantId(azureCreds.TenantID)
			azureCloudInfo.SetAzuRG(azureCreds.ResourceGroup)
		} else {
			azureCloudInfo.SetAzuClientId(azureCreds.ClientID)

			azureCreds.ClientSecret, err = cmd.Flags().GetString("client-secret")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			azureCloudInfo.SetAzuClientSecret(azureCreds.ClientSecret)

			azureCreds.SubscriptionID, err = cmd.Flags().GetString("subscription-id")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			azureCloudInfo.SetAzuSubscriptionId(azureCreds.SubscriptionID)

			azureCreds.TenantID, err = cmd.Flags().GetString("tenant-id")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			azureCloudInfo.SetAzuTenantId(azureCreds.TenantID)

			azureCreds.ResourceGroup, err = cmd.Flags().GetString("rg")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			azureCloudInfo.SetAzuRG(azureCreds.ResourceGroup)
		}
		hostedZoneID, err := cmd.Flags().GetString("hosted-zone-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(hostedZoneID) > 0 {
			azureCloudInfo.SetAzuHostedZoneId(hostedZoneID)
		}

		networkSubscriptionID, err := cmd.Flags().GetString("network-subscription-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(networkSubscriptionID) > 0 {
			azureCloudInfo.SetAzuNetworkSubscriptionId(networkSubscriptionID)
		}

		networkRG, err := cmd.Flags().GetString("network-rg")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(networkRG) > 0 {
			azureCloudInfo.SetAzuNetworkRG(networkRG)
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

		zones, err := cmd.Flags().GetStringArray("zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		imageBundles, err := cmd.Flags().GetStringArray("image-bundle")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.Provider{
			Code:          util.GetStringPointer(providerCode),
			AllAccessKeys: allAccessKeys,
			ImageBundles:  buildAzureImageBundles(imageBundles),
			Name:          util.GetStringPointer(providerName),
			Regions:       buildAzureRegions(regions, zones),
			Details: &ybaclient.ProviderDetails{
				AirGapInstall: util.GetBoolPointer(airgapInstall),
				NtpServers:    ntpServers,
				CloudInfo: &ybaclient.CloudInfo{
					Azu: &azureCloudInfo,
				},
			},
		}

		rTask, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Provider: Azure", "Create")
		}

		providerutil.WaitForCreateProviderTask(
			authAPI, providerName, rTask, providerCode)
	},
}

func init() {
	createAzureProviderCmd.Flags().SortFlags = false
	// Flags needed for Azure
	createAzureProviderCmd.Flags().String("client-id", "",
		fmt.Sprintf("Azure Client ID. "+
			"Can also be set using environment variable %s.", util.AzureClientIDEnv))
	createAzureProviderCmd.Flags().String("client-secret", "",
		fmt.Sprintf("Azure Client Secret. "+
			"Can also be set using environment variable %s.", util.AzureClientSecretEnv))
	createAzureProviderCmd.Flags().String("tenant-id", "",
		fmt.Sprintf("Azure Tenant ID. "+
			"Can also be set using environment variable %s.", util.AzureTenantIDEnv))
	createAzureProviderCmd.Flags().String("subscription-id", "",
		fmt.Sprintf("Azure Subscription ID. "+
			"Can also be set using environment variable %s.", util.AzureSubscriptionIDEnv))
	createAzureProviderCmd.Flags().String("rg", "",
		fmt.Sprintf("Azure Resource Group. "+
			"Can also be set using environment variable %s.", util.AzureRGEnv))
	createAzureProviderCmd.MarkFlagsRequiredTogether("client-id", "client-secret", "rg",
		"subscription-id", "tenant-id")

	createAzureProviderCmd.Flags().String("network-subscription-id", "",
		"[Optional] Azure Network Subscription ID. All network resources and NIC "+
			"resouce of VMs will be created in this group. If left empty, "+
			"the default subscription ID will be used.")
	createAzureProviderCmd.Flags().String("network-rg", "",
		"[Optional] Azure Network Resource Group. All network resources and "+
			"NIC resouce of VMs will be created in this group. If left empty, "+
			"the default resource group will be used.")

	createAzureProviderCmd.Flags().String("hosted-zone-id", "",
		"[Optional] Hosted Zone ID corresponding to Private DNS Zone.")

	createAzureProviderCmd.Flags().StringArray("region", []string{},
		"[Required] Region associated with the Azure provider. Minimum number of required "+
			"regions = 1. Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"region-name=<region-name>::vnet=<virtual-network>::sg-id=<security-group-id>::"+
			"rg=<resource-group>::network-rg=<network-resource-group>\". "+
			formatter.Colorize("Region name and Virtual network are required key-values.",
				formatter.GreenColor)+
			" Security Group ID, Resource Group (override for this region) and Network "+
			"Resource Group (override for this region) are optional. "+
			"Each region needs to be added using a separate --region flag. "+
			"Example: --region region-name=westus2::vnet=<vnet-id>")
	createAzureProviderCmd.Flags().StringArray("zone", []string{},
		"[Required] Zone associated to the Azure Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>\"."+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Secondary subnet ID is optional. Each --region definition "+
			"must have atleast one corresponding --zone definition. Multiple --zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --zone flag. "+
			"Example: --zone zone-name=westus2-1::region-name=westus2::subnet=<subnet-id>")

	createAzureProviderCmd.Flags().StringArray("image-bundle", []string{},
		"[Optional] Intel x86_64 image bundles associated with Azure provider. "+
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

	createAzureProviderCmd.Flags().String("custom-ssh-keypair-name", "",
		"[Optional] Provide custom key pair name to access YugabyteDB nodes. "+
			"If left empty, "+
			"YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.")
	createAzureProviderCmd.Flags().String("custom-ssh-keypair-file-path", "",
		"[Optional] Provide custom key pair file path to access YugabyteDB nodes. "+
			formatter.Colorize("Required with --custom-ssh-keypair-name.",
				formatter.GreenColor))
	createAzureProviderCmd.MarkFlagsRequiredTogether("custom-ssh-keypair-name",
		"custom-ssh-keypair-file-path")

	createAzureProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads. "+
			"(default false)")
	createAzureProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")
}

func buildAzureRegions(regionStrings, zoneStrings []string) (res []ybaclient.Region) {
	if len(regionStrings) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one region is required per provider.\n",
				formatter.RedColor))
	}
	for _, regionString := range regionStrings {
		region := providerutil.BuildRegionMapFromString(regionString, "")
		if _, ok := region["vnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Virtual Network not specified in region info.\n",
					formatter.RedColor))

		}

		zones := buildAzureZones(zoneStrings, region["name"])
		r := ybaclient.Region{
			Code: util.GetStringPointer(region["name"]),
			Name: util.GetStringPointer(region["name"]),
			Details: &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Azu: &ybaclient.AzureRegionCloudInfo{
						SecurityGroupId:      util.GetStringPointer(region["sg-id"]),
						Vnet:                 util.GetStringPointer(region["vnet"]),
						AzuNetworkRGOverride: util.GetStringPointer(region["network-rg"]),
						AzuRGOverride:        util.GetStringPointer(region["rg"]),
					},
				},
			},
			Zones: zones,
		}
		res = append(res, r)
	}
	return res
}

func buildAzureZones(zoneStrings []string, regionName string) (res []ybaclient.AvailabilityZone) {
	for _, zoneString := range zoneStrings {
		zone := providerutil.BuildZoneMapFromString(zoneString, "")

		if _, ok := zone["subnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Subnet not specified in zone info.\n",
					formatter.RedColor))
		}

		if strings.Compare(zone["region-name"], regionName) == 0 {
			z := ybaclient.AvailabilityZone{
				Code:            util.GetStringPointer(zone["name"]),
				Name:            zone["name"],
				SecondarySubnet: util.GetStringPointer(zone["secondary-subnet"]),
				Subnet:          util.GetStringPointer(zone["subnet"]),
			}
			res = append(res, z)
		}
	}
	if len(res) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}
	return res
}

func buildAzureImageBundles(imageBundles []string) []ybaclient.ImageBundle {
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
