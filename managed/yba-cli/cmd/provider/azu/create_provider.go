/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"fmt"
	"os"
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
	Use:   "create",
	Short: "Create an Azure YugabyteDB Anywhere provider",
	Long:  "Create an Azure provider in YugabyteDB Anywhere",
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

		sshUser, err := cmd.Flags().GetString("ssh-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sshPort, err := cmd.Flags().GetInt("ssh-port")
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

		requestBody := ybaclient.Provider{
			Code:          util.GetStringPointer(providerCode),
			AllAccessKeys: &allAccessKeys,
			Name:          util.GetStringPointer(providerName),
			Regions:       buildAzureRegions(regions, zones),
			Details: &ybaclient.ProviderDetails{
				AirGapInstall: util.GetBoolPointer(airgapInstall),
				SshPort:       util.GetInt32Pointer(int32(sshPort)),
				SshUser:       util.GetStringPointer(sshUser),
				NtpServers:    util.StringSliceFromString(ntpServers),
				CloudInfo: &ybaclient.CloudInfo{
					Azu: &azureCloudInfo,
				},
			},
		}

		rCreate, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider: Azure", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rCreate.GetResourceUUID()
		taskUUID := rCreate.GetTaskUUID()

		providerutil.WaitForCreateProviderTask(authAPI,
			providerName, providerUUID, providerCode, taskUUID)
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
		"Azure Network Subscription ID.")
	createAzureProviderCmd.Flags().String("network-rg", "", "Azure Network Resource Group.")

	createAzureProviderCmd.Flags().String("hosted-zone-id", "",
		"[Optional] Hosted Zone ID corresponging to Private DNS Zone.")

	createAzureProviderCmd.Flags().StringArray("region", []string{},
		"[Required] Region associated with the Azure provider. Minimum number of required "+
			"regions = 1. Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,"+
			"vnet=<virtual-network>,sg-id=<security-group-id>,yb-image=<custom-ami>\". "+
			formatter.Colorize("Region name and Virtual network are required key-values.",
				formatter.GreenColor)+
			" Security Group ID and YB Image (AMI) are optional. "+
			"Each region needs to be added using a separate --region flag. "+
			"Example: --region region-name=westus2,vnet=<vent-id>")
	createAzureProviderCmd.Flags().StringArray("zone", []string{},
		"[Required] Zone associated to the Azure Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>\"."+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Secondary subnet ID is optional. Each --region definition "+
			"must have atleast one corresponding --zone definition. Multiple --zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --zone flag. "+
			"Example: --zone zone-name=westus2-1,region-name=westus2,subnet=<subnet-id>")

	createAzureProviderCmd.Flags().String("ssh-user", "",
		"[Optional] SSH User to access the YugabyteDB nodes.")
	createAzureProviderCmd.Flags().Int("ssh-port", 22,
		"[Optional] SSH Port to access the YugabyteDB nodes.")
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
			" lacking access to the public internet for package downloads, "+
			"defaults to false.")
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
						SecurityGroupId: util.GetStringPointer(region["sg-id"]),
						Vnet:            util.GetStringPointer(region["vnet"]),
						YbImage:         util.GetStringPointer(region["yb-image"]),
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
