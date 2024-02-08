/*
 * Copyright (c) YugaByte, Inc.
 */

package create

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createAzureProviderCmd represents the provider command
var createAzureProviderCmd = &cobra.Command{
	Use:   "azure [provider-name]",
	Short: "Create an Azure YugabyteDB Anywhere provider",
	Long:  "Create an Azure provider in YugabyteDB Anywhere",
	Args:  cobra.MaximumNArgs(1),
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, _ := cmd.Flags().GetString("name")
		if !(len(args) > 0 || len(providerNameFlag) > 0) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to create\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		providerNameFlag, _ := cmd.Flags().GetString("name")
		var providerName string
		if len(args) > 0 {
			providerName = args[0]
		} else if len(providerNameFlag) > 0 {
			providerName = providerNameFlag
		}

		providerCode := "azu"
		config, err := buildAzureConfig(cmd)
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

		zones, err := cmd.Flags().GetStringArray("zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.Provider{
			Code:                 util.GetStringPointer(providerCode),
			Config:               util.StringMap(config),
			Name:                 util.GetStringPointer(providerName),
			AirGapInstall:        util.GetBoolPointer(airgapInstall),
			SshPort:              util.GetInt32Pointer(int32(sshPort)),
			SshUser:              util.GetStringPointer(sshUser),
			KeyPairName:          util.GetStringPointer(keyPairName),
			SshPrivateKeyContent: util.GetStringPointer(sshFileContent),
			Regions:              buildAzureRegions(regions, zones),
		}

		rCreate, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider", "Create Azure")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rCreate.GetResourceUUID()
		taskUUID := rCreate.GetTaskUUID()

		waitForCreateProviderTask(authAPI, providerName, providerUUID, taskUUID)
	},
}

func init() {
	createAzureProviderCmd.Flags().SortFlags = false

	createAzureProviderCmd.Flags().StringP("name", "n", "",
		"[Optional] The name of the provider to be created.")

	// Flags needed for Azure
	createAzureProviderCmd.Flags().String("az-client-id", "",
		fmt.Sprintf("Azure Client ID. "+
			"Can also be set using environment variable %s.", util.AzureClientIDEnv))
	createAzureProviderCmd.Flags().String("az-client-secret", "",
		fmt.Sprintf("Azure Client Secret. "+
			"Can also be set using environment variable %s.", util.AzureClientSecretEnv))
	createAzureProviderCmd.Flags().String("az-tenant-id", "",
		fmt.Sprintf("Azure Tenant ID. "+
			"Can also be set using environment variable %s.", util.AzureTenantIDEnv))
	createAzureProviderCmd.Flags().String("az-subscription-id", "",
		fmt.Sprintf("Azure Subscription ID. "+
			"Can also be set using environment variable %s.", util.AzureSubscriptionIDEnv))
	createAzureProviderCmd.Flags().String("az-rg", "",
		fmt.Sprintf("Azure Resource Group. "+
			"Can also be set using environment variable %s.", util.AzureRGEnv))
	createAzureProviderCmd.MarkFlagsRequiredTogether("az-client-id", "az-client-secret", "az-rg",
		"az-subscription-id", "az-tenant-id")

	createAzureProviderCmd.Flags().String("az-network-subscription-id", "",
		"Azure Network Subscription ID.")
	createAzureProviderCmd.Flags().String("az-network-rg", "", "Azure Resource Group.")

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
			"Each region needs to be added using a separate --region flag.")
	createAzureProviderCmd.Flags().StringArray("zone", []string{},
		"[Required] Zone associated to the Azure Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>\"."+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Secondary subnet ID is optional. Each --region definition "+
			"must have atleast one corresponding --zone definition. Multiple --zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --zone flag.")

	createAzureProviderCmd.Flags().String("ssh-user", "",
		"[Optional] SSH User to access the YugabyteDB nodes.")
	createAzureProviderCmd.Flags().Int("ssh-port", 22,
		"[Optional] SSH Port to access the YugabyteDB nodes, defaults to 22.")
	createAzureProviderCmd.Flags().String("custom-ssh-keypair-name", "",
		"[Optional] Provider custom key pair name to access YugabyteDB nodes. "+
			"YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.")
	createAzureProviderCmd.Flags().String("custom-ssh-keypair-file-path", "",
		"[Optional] Provider custom key pair file path to access YugabyteDB nodes. "+
			formatter.Colorize("Required with --custom-ssh-keypair-name.",
				formatter.GreenColor))
	createAzureProviderCmd.MarkFlagsRequiredTogether("custom-ssh-keypair-name",
		"custom-ssh-keypair-file-path")

	createAzureProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Do YugabyteDB nodes have access to public internet to download packages.")

	setAzureDefaults()

}

func setAzureDefaults() {
	viper.SetDefault("ssh-port", 22)
	viper.SetDefault("airgap-install", false)
}

func buildAzureConfig(cmd *cobra.Command) (map[string]interface{}, error) {
	config := make(map[string]interface{})

	var err error

	var azureCreds util.AzureCredentials

	azureCreds.ClientID, err = cmd.Flags().GetString("az-client-id")
	if err != nil {
		return nil, err
	}
	if len(azureCreds.ClientID) == 0 {

		azureCreds, err = util.AzureCredentialsFromEnv()
		if err != nil {
			return nil, err
		}
		config[util.AzureClientIDEnv] = azureCreds.ClientID
		config[util.AzureClientSecretEnv] = azureCreds.ClientSecret
		config[util.AzureSubscriptionIDEnv] = azureCreds.SubscriptionID
		config[util.AzureTenantIDEnv] = azureCreds.TenantID
		config[util.AzureRGEnv] = azureCreds.ResourceGroup
	} else {
		config[util.AzureClientIDEnv] = azureCreds.ClientID

		azureCreds.ClientSecret, err = cmd.Flags().GetString("az-client-secret")
		if err != nil {
			return nil, err
		}
		config[util.AzureClientSecretEnv] = azureCreds.ClientSecret

		azureCreds.SubscriptionID, err = cmd.Flags().GetString("az-subscription-id")
		if err != nil {
			return nil, err
		}
		config[util.AzureSubscriptionIDEnv] = azureCreds.SubscriptionID

		azureCreds.TenantID, err = cmd.Flags().GetString("az-tenant-id")
		if err != nil {
			return nil, err
		}
		config[util.AzureTenantIDEnv] = azureCreds.TenantID
		azureCreds.ResourceGroup, err = cmd.Flags().GetString("az-rg")
		if err != nil {
			return nil, err
		}
		config[util.AzureRGEnv] = azureCreds.ResourceGroup
	}
	hostedZoneID, err := cmd.Flags().GetString("hosted-zone-id")
	if err != nil {
		return nil, err
	}
	if len(hostedZoneID) > 0 {
		config["HOSTED_ZONE_ID"] = hostedZoneID
	}

	networkSubscriptionID, err := cmd.Flags().GetString("az-network-subscription-id")
	if err != nil {
		return nil, err
	}
	if len(networkSubscriptionID) > 0 {
		config["AZURE_NETWORK_SUBSCRIPTION_ID"] = networkSubscriptionID
	}

	networkRG, err := cmd.Flags().GetString("az-network-rg")
	if err != nil {
		return nil, err
	}
	if len(networkRG) > 0 {
		config["AZURE_NETWORK_RG"] = networkRG
	}

	return config, nil
}

func buildAzureRegions(regionStrings, zoneStrings []string) (res []ybaclient.Region) {
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
				}
			case "vnet":
				if len(strings.TrimSpace(val)) != 0 {
					region["vnet"] = val
				}
			case "sg-id":
				if len(strings.TrimSpace(val)) != 0 {
					region["sg-id"] = val
				}
			case "yb-image":
				if len(strings.TrimSpace(val)) != 0 {
					region["yb-image"] = val
				}
			}
		}
		if _, ok := region["name"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Name not specified in region.",
					formatter.RedColor))
		}
		if _, ok := region["vnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Virtual Network not specified in region info.",
					formatter.RedColor))

		}

		zones := buildAzureZones(zoneStrings, region["name"])
		r := ybaclient.Region{
			Code:            util.GetStringPointer(region["name"]),
			Name:            util.GetStringPointer(region["name"]),
			SecurityGroupId: util.GetStringPointer(region["sg-id"]),
			VnetName:        util.GetStringPointer(region["vnet"]),
			YbImage:         util.GetStringPointer(region["yb-image"]),
			Zones:           zones,
		}
		res = append(res, r)
	}
	return res
}

func buildAzureZones(zoneStrings []string, regionName string) (res []ybaclient.AvailabilityZone) {
	for _, zoneString := range zoneStrings {
		zone := map[string]string{}
		for _, zoneInfo := range strings.Split(zoneString, ",") {
			kvp := strings.Split(zoneInfo, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in zone description",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "zone-name":
				if len(strings.TrimSpace(val)) != 0 {
					zone["name"] = val
				}
			case "region-name":
				if len(strings.TrimSpace(val)) != 0 {
					zone["region-name"] = val
				}
			case "subnet":
				if len(strings.TrimSpace(val)) != 0 {
					zone["subnet"] = val
				}
			case "secondary-subnet":
				if len(strings.TrimSpace(val)) != 0 {
					zone["secondary-subnet"] = val
				}
			}
		}
		if _, ok := zone["name"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Name not specified in zone.",
					formatter.RedColor))
		}
		if _, ok := zone["region-name"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Region name not specified in zone.",
					formatter.RedColor))
		}
		if _, ok := zone["subnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Subnet not specified in zone info.",
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
			formatter.Colorize("Atleast one zone is required per region.",
				formatter.RedColor))
	}
	return res
}
