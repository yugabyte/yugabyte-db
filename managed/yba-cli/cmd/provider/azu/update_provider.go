/*
 * Copyright (c) YugaByte, Inc.
 */

package azu

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateAzureProviderCmd represents the provider command
var updateAzureProviderCmd = &cobra.Command{
	Use:   "update",
	Short: "Update an Azure YugabyteDB Anywhere provider",
	Long:  "Update an Azure provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to update\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		providerName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerListRequest := authAPI.GetListOfProviders()
		providerListRequest = providerListRequest.Name(providerName)

		r, response, err := providerListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Provider: Azure", "Update - Fetch Providers")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		var provider ybaclient.Provider
		providerCode := util.AzureProviderType
		for _, p := range r {
			if p.GetCode() == providerCode {
				provider = p
			}
		}

		if len(strings.TrimSpace(provider.GetName())) == 0 {
			errMessage := fmt.Sprintf(
				"No provider %s in cloud type %s.\n", providerName, providerCode)
			logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
		}

		providerRegions := provider.GetRegions()
		details := provider.GetDetails()
		cloudInfo := details.GetCloudInfo()
		azureCloudInfo := cloudInfo.GetAzu()

		newProviderName, err := cmd.Flags().GetString("new-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(newProviderName) > 0 {
			logrus.Debug("Updating provider name\n")
			provider.SetName(newProviderName)
			providerName = newProviderName
		}

		// Updating CloudInfo

		var azureCreds util.AzureCredentials

		azureCreds.ResourceGroup, err = cmd.Flags().GetString("rg")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(azureCreds.ResourceGroup) > 0 {
			logrus.Debug("Updating Azure Resource Group\n")
			azureCloudInfo.SetAzuClientId(azureCreds.ResourceGroup)
		}

		azureCreds.ClientID, err = cmd.Flags().GetString("client-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(azureCreds.ClientID) > 0 {
			logrus.Debug("Updating Azure Client ID\n")
			azureCloudInfo.SetAzuClientId(azureCreds.ClientID)
		}

		azureCreds.ClientSecret, err = cmd.Flags().GetString("client-secret")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(azureCreds.ClientSecret) > 0 {
			logrus.Debug("Updating Azure Client Secret\n")
			azureCloudInfo.SetAzuClientId(azureCreds.ClientSecret)
		}

		azureCreds.TenantID, err = cmd.Flags().GetString("tenant-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(azureCreds.TenantID) > 0 {
			logrus.Debug("Updating Azure Tenant ID\n")
			azureCloudInfo.SetAzuClientId(azureCreds.TenantID)
		}

		azureCreds.SubscriptionID, err = cmd.Flags().GetString("subscription-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(azureCreds.SubscriptionID) > 0 {
			logrus.Debug("Updating Azure Subscription ID\n")
			azureCloudInfo.SetAzuClientId(azureCreds.SubscriptionID)
		}

		networkRG, err := cmd.Flags().GetString("network-rg")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(networkRG) > 0 {
			logrus.Debug("Updating Azure Network Resource Group\n")
			azureCloudInfo.SetAzuClientId(networkRG)
		}

		networkSubscriptionID, err := cmd.Flags().GetString("network-subscription-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(networkSubscriptionID) > 0 {
			logrus.Debug("Updating Azure Network Subscription ID\n")
			azureCloudInfo.SetAzuClientId(networkSubscriptionID)
		}

		hostedZoneID, err := cmd.Flags().GetString("hosted-zone-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(hostedZoneID) > 0 {
			logrus.Debug("Updating Hosted Zone ID\n")
			azureCloudInfo.SetAzuHostedZoneId(hostedZoneID)
		}

		cloudInfo.SetAzu(azureCloudInfo)
		details.SetCloudInfo(cloudInfo)

		// End of Updating CloudInfo

		// Update ProviderDetails

		if cmd.Flags().Changed("airgap-install") {
			logrus.Debug("Updating airgap install\n")
			airgapInstall, err := cmd.Flags().GetBool("airgap-install")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			details.SetAirGapInstall(airgapInstall)
		}

		sshUser, err := cmd.Flags().GetString("ssh-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(sshUser) > 0 {
			logrus.Debug("Updating SSH user\n")
			details.SetSshUser(sshUser)
		}

		if cmd.Flags().Changed("ssh-port") {
			sshPort, err := cmd.Flags().GetInt("ssh-port")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if details.GetSshPort() != int32(sshPort) {
				logrus.Debug("Updating SSH port\n")
				details.SetSshPort(int32(sshPort))
			}
		}

		ntpServers, err := cmd.Flags().GetStringArray("ntp-servers")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(ntpServers) > 0 {
			logrus.Debug("Updating NTP servers\n")
			details.SetNtpServers(ntpServers)
		}

		provider.SetDetails(details)

		// End of Updating ProviderDetails

		// Update Regions

		addRegions, err := cmd.Flags().GetStringArray("add-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		addZones, err := cmd.Flags().GetStringArray("add-zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		editRegions, err := cmd.Flags().GetStringArray("edit-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		editZones, err := cmd.Flags().GetStringArray("edit-zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		removeRegions, err := cmd.Flags().GetStringArray("remove-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		removeZones, err := cmd.Flags().GetStringArray("remove-zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerRegions = removeAzureRegions(removeRegions, providerRegions)

		providerRegions = editAzureRegions(editRegions, addZones, editZones, removeZones, providerRegions)

		providerRegions = addAzureRegions(addRegions, addZones, providerRegions)

		provider.SetRegions(providerRegions)
		// End of Updating Regions

		rUpdate, response, err := authAPI.EditProvider(provider.GetUuid()).
			EditProviderRequest(provider).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider: Azure", "Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rUpdate.GetResourceUUID()
		taskUUID := rUpdate.GetTaskUUID()

		providerutil.WaitForUpdateProviderTask(
			authAPI, providerName, providerUUID, providerCode, taskUUID)
	},
}

func init() {
	updateAzureProviderCmd.Flags().SortFlags = false

	updateAzureProviderCmd.Flags().String("new-name", "",
		"[Optional] Updating provider name.")

	updateAzureProviderCmd.Flags().String("client-id", "",
		fmt.Sprintf("[Optional] Update Azure Client ID. %s",
			formatter.Colorize("Required with client-secret, tenant-id, "+
				"subscription-id, rg",
				formatter.GreenColor)))
	updateAzureProviderCmd.Flags().String("client-secret", "",
		fmt.Sprintf("[Optional] Update Azure Client Secret. %s",
			formatter.Colorize("Required with client-id, tenant-id, "+
				"subscription-id, rg",
				formatter.GreenColor)))
	updateAzureProviderCmd.Flags().String("tenant-id", "",
		fmt.Sprintf("[Optional] Update Azure Tenant ID. %s",
			formatter.Colorize("Required with client-secret, client-id, "+
				"subscription-id, rg",
				formatter.GreenColor)))
	updateAzureProviderCmd.Flags().String("subscription-id", "",
		fmt.Sprintf("[Optional] Update Azure Subscription ID. %s",
			formatter.Colorize("Required with client-id, client-secret, tenant-id,"+
				" rg",
				formatter.GreenColor)))
	updateAzureProviderCmd.Flags().String("rg", "",
		fmt.Sprintf("[Optional] Update Azure Resource Group. %s",
			formatter.Colorize("Required with client-id, client-secret, tenant-id, "+
				"subscription-id",
				formatter.GreenColor)))

	updateAzureProviderCmd.Flags().String("network-subscription-id", "",
		"[Optional] Update Azure Network Subscription ID.")
	updateAzureProviderCmd.Flags().String("network-rg", "",
		"[Optional] Update Azure Network Resource Group.")

	updateAzureProviderCmd.Flags().String("hosted-zone-id", "",
		"[Optional] Update Hosted Zone ID corresponging to Private DNS Zone.")

	updateAzureProviderCmd.Flags().StringArray("add-region", []string{},
		"[Optional] Add region associated with the Azure provider. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,"+
			"vnet=<virtual-network>,sg-id=<security-group-id>,yb-image=<custom-ami>\". "+
			formatter.Colorize("Region name and Virtual network are required key-values.",
				formatter.GreenColor)+
			" Security Group ID and YB Image (AMI) are optional. "+
			"Each region needs to be added using a separate --add-region flag.")
	updateAzureProviderCmd.Flags().StringArray("add-zone", []string{},
		"[Optional] Zone associated to the Azure Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>\"."+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Secondary subnet ID is optional. Each --add-region definition "+
			"must have atleast one corresponding --add-zone definition. Multiple"+
			" --add-zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --add-zone flag.")

	updateAzureProviderCmd.Flags().StringArray("remove-region", []string{},
		"[Optional] Region name to be removed from the provider. "+
			"Each region to be removed needs to be provided using a separate "+
			"--remove-region definition. Removing a region removes the corresponding zones.")
	updateAzureProviderCmd.Flags().StringArray("remove-zone", []string{},
		"[Optional] Remove zone associated to the Azure Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Each zone needs to be removed using a separate --remove-zone flag.")

	updateAzureProviderCmd.Flags().StringArray("edit-region", []string{},
		"[Optional] Edit region details associated with the Azure provider. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,"+
			"vnet=<virtual-network>,sg-id=<security-group-id>,yb-image=<custom-ami>\". "+
			formatter.Colorize("Region name is a required key-value pair.",
				formatter.GreenColor)+
			" Virtual network and Security Group ID are optional. "+
			"Each region needs to be modified using a separate --edit-region flag.")
	updateAzureProviderCmd.Flags().StringArray("edit-zone", []string{},
		"[Optional] Edit zone associated to the Azure Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>,"+
			"secondary-subnet=<secondary-subnet-id>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Subnet IDs and Secondary subnet ID is optional. "+
			"Each zone needs to be modified using a separate --edit-zone flag.")

	updateAzureProviderCmd.Flags().String("ssh-user", "",
		"[Optional] Updating SSH User to access the YugabyteDB nodes.")
	updateAzureProviderCmd.Flags().Int("ssh-port", 0,
		"[Optional] Updating SSH Port to access the YugabyteDB nodes.")

	updateAzureProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads. "+
			"(default false)")
	updateAzureProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")

}

func removeAzureRegions(
	removeRegions []string,
	providerRegions []ybaclient.Region) []ybaclient.Region {
	if len(removeRegions) == 0 {
		return providerRegions
	}

	for _, r := range removeRegions {
		for i, pR := range providerRegions {
			if strings.Compare(pR.GetCode(), r) == 0 {
				pR.SetActive(false)
				providerRegions[i] = pR
			}
		}
	}

	return providerRegions
}

func editAzureRegions(
	editRegions, addZones, editZones, removeZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {

	for i, r := range providerRegions {
		regionName := r.GetCode()
		zones := r.GetZones()
		zones = removeAzureZones(regionName, removeZones, zones)
		zones = editAzureZones(regionName, editZones, zones)
		zones = addAzureZones(regionName, addZones, zones)
		r.SetZones(zones)
		if len(editRegions) != 0 {
			for _, regionString := range editRegions {
				region := providerutil.BuildRegionMapFromString(regionString, "edit")

				if strings.Compare(region["name"], regionName) == 0 {
					details := r.GetDetails()
					cloudInfo := details.GetCloudInfo()
					azu := cloudInfo.GetAzu()
					if len(region["vnet"]) != 0 {
						azu.SetVnet(region["vnet"])
					}
					if len(region["sg-id"]) != 0 {
						azu.SetSecurityGroupId(region["sg-id"])
					}
					cloudInfo.SetAzu(azu)
					details.SetCloudInfo(cloudInfo)
					r.SetDetails(details)

				}

			}
		}
		providerRegions[i] = r
	}
	return providerRegions
}

func addAzureRegions(
	addRegions, addZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {
	if len(addRegions) == 0 {
		return providerRegions
	}
	for _, regionString := range addRegions {
		region := providerutil.BuildRegionMapFromString(regionString, "add")

		zones := addAzureZones(region["name"], addZones, make([]ybaclient.AvailabilityZone, 0))
		r := ybaclient.Region{
			Code:  util.GetStringPointer(region["name"]),
			Name:  util.GetStringPointer(region["name"]),
			Zones: zones,
			Details: &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Azu: &ybaclient.AzureRegionCloudInfo{
						YbImage:         util.GetStringPointer(region["yb-image"]),
						SecurityGroupId: util.GetStringPointer(region["sg-id"]),
						Vnet:            util.GetStringPointer(region["vnet"]),
					},
				},
			},
		}

		providerRegions = append(providerRegions, r)
	}

	return providerRegions
}

func removeAzureZones(
	regionName string,
	removeZones []string,
	zones []ybaclient.AvailabilityZone,
) []ybaclient.AvailabilityZone {
	if len(removeZones) == 0 {
		if len(zones) == 0 {
			logrus.Fatalln(
				formatter.Colorize("Atleast one zone is required per region.\n",
					formatter.RedColor))
		}
		return zones
	}
	for _, zoneString := range removeZones {
		zone := providerutil.BuildZoneMapFromString(zoneString, "remove")

		if strings.Compare(zone["region-name"], regionName) == 0 {
			for i, az := range zones {
				if strings.Compare(az.GetCode(), zone["name"]) == 0 {
					az.SetActive(false)
					zones[i] = az
				}
			}
		}
	}
	if len(zones) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}
	return zones
}

func editAzureZones(
	regionName string,
	editZones []string,
	zones []ybaclient.AvailabilityZone,
) []ybaclient.AvailabilityZone {
	if len(editZones) == 0 {
		if len(zones) == 0 {
			logrus.Fatalln(
				formatter.Colorize("Atleast one zone is required per region.\n",
					formatter.RedColor))
		}
		return zones
	}
	for _, zoneString := range editZones {
		zone := providerutil.BuildZoneMapFromString(zoneString, "edit")

		if strings.Compare(zone["region-name"], regionName) == 0 {
			for i, az := range zones {
				if strings.Compare(az.GetCode(), zone["name"]) == 0 {
					if len(zone["subnet"]) != 0 {
						az.SetSubnet(zone["subnet"])
					}
					if len(zone["secondary-subnet"]) != 0 {
						az.SetSecondarySubnet(zone["secondary-subnet"])
					}
					zones[i] = az
				}
			}
		}
	}
	if len(zones) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}

	return zones
}

func addAzureZones(
	regionName string,
	addZones []string,
	zones []ybaclient.AvailabilityZone,
) []ybaclient.AvailabilityZone {
	if len(addZones) == 0 {
		if len(zones) == 0 {
			logrus.Fatalln(
				formatter.Colorize("Atleast one zone is required per region.\n",
					formatter.RedColor))
		}
		return zones
	}
	for _, zoneString := range addZones {
		zone := providerutil.BuildZoneMapFromString(zoneString, "add")

		if _, ok := zone["subnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Subnet not specified in add-zone.\n",
					formatter.RedColor))
		}

		if strings.Compare(zone["region-name"], regionName) == 0 {
			z := ybaclient.AvailabilityZone{
				Code:            util.GetStringPointer(zone["name"]),
				Name:            zone["name"],
				SecondarySubnet: util.GetStringPointer(zone["secondary-subnet"]),
				Subnet:          util.GetStringPointer(zone["subnet"]),
			}
			zones = append(zones, z)
		}
	}
	if len(zones) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}

	return zones
}
