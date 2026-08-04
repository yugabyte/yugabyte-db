/*
 * Copyright (c) YugabyteDB, Inc.
 */

package azu

import (
	"fmt"
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

// updateAzureProviderCmd represents the provider command
var updateAzureProviderCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an Azure YugabyteDB Anywhere provider",
	Long:    "Update an Azure provider in YugabyteDB Anywhere",
	Example: `yba provider azure update --name <provider-name> \
	 --hosted-zone-id <hosted-zone-id>`,
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
			util.FatalHTTPError(response, err, "Provider: Azure", "Update - Fetch Providers")
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

		if util.IsEmptyString(provider.GetName()) {
			errMessage := fmt.Sprintf(
				"No provider %s in cloud type %s.\n", providerName, providerCode)
			logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
		}

		providerRegions := provider.GetRegions()
		details := provider.GetDetails()
		cloudInfo := details.GetCloudInfo()
		azureCloudInfo := cloudInfo.GetAzu()

		providerImageBundles := provider.GetImageBundles()

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
			azureCloudInfo.SetAzuRG(azureCreds.ResourceGroup)
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
			azureCloudInfo.SetAzuClientSecret(azureCreds.ClientSecret)
		}

		azureCreds.TenantID, err = cmd.Flags().GetString("tenant-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(azureCreds.TenantID) > 0 {
			logrus.Debug("Updating Azure Tenant ID\n")
			azureCloudInfo.SetAzuTenantId(azureCreds.TenantID)
		}

		azureCreds.SubscriptionID, err = cmd.Flags().GetString("subscription-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(azureCreds.SubscriptionID) > 0 {
			logrus.Debug("Updating Azure Subscription ID\n")
			azureCloudInfo.SetAzuSubscriptionId(azureCreds.SubscriptionID)
		}

		networkRG, err := cmd.Flags().GetString("network-rg")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(networkRG) > 0 {
			logrus.Debug("Updating Azure Network Resource Group\n")
			azureCloudInfo.SetAzuNetworkRG(networkRG)
		}

		networkSubscriptionID, err := cmd.Flags().GetString("network-subscription-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(networkSubscriptionID) > 0 {
			logrus.Debug("Updating Azure Network Subscription ID\n")
			azureCloudInfo.SetAzuNetworkSubscriptionId(networkSubscriptionID)
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

		providerRegions = editAzureRegions(
			editRegions,
			addZones,
			editZones,
			removeZones,
			providerRegions,
		)

		providerRegions = addAzureRegions(addRegions, addZones, providerRegions)

		provider.SetRegions(providerRegions)
		// End of Updating Regions

		// Update Image Bundles

		addImageBundles, err := cmd.Flags().GetStringArray("add-image-bundle")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		editImageBundles, err := cmd.Flags().GetStringArray("edit-image-bundle")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		removeImageBundles, err := cmd.Flags().GetStringArray("remove-image-bundle")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerImageBundles = removeAzureImageBundles(removeImageBundles, providerImageBundles)

		providerImageBundles = editAzureImageBundles(
			editImageBundles,
			providerImageBundles)

		providerImageBundles = addAzureImageBundles(
			addImageBundles,
			providerImageBundles,
		)

		provider.SetImageBundles(providerImageBundles)

		// End of Updating Image Bundles

		rTask, response, err := authAPI.EditProvider(provider.GetUuid()).
			EditProviderRequest(provider).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Provider: Azure", "Update")
		}

		providerutil.WaitForUpdateProviderTask(
			authAPI, providerName, rTask, providerCode)
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
		"[Optional] Update Hosted Zone ID corresponding to Private DNS Zone.")

	updateAzureProviderCmd.Flags().StringArray("add-region", []string{},
		"[Optional] Add region associated with the Azure provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"region-name=<region-name>::"+
			"vnet=<virtual-network>::sg-id=<security-group-id>\". "+
			formatter.Colorize("Region name and Virtual network are required key-values.",
				formatter.GreenColor)+
			" Security Group ID is optional. "+
			"Each region needs to be added using a separate --add-region flag.")
	updateAzureProviderCmd.Flags().StringArray("add-zone", []string{},
		"[Optional] Zone associated to the Azure Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>\"."+
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
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"zone-name=<zone-name>::region-name=<region-name>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Each zone needs to be removed using a separate --remove-zone flag.")

	updateAzureProviderCmd.Flags().StringArray("edit-region", []string{},
		"[Optional] Edit region details associated with the Azure provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"region-name=<region-name>::"+
			"vnet=<virtual-network>::sg-id=<security-group-id>\". "+
			formatter.Colorize("Region name is a required key-value pair.",
				formatter.GreenColor)+
			" Virtual network and Security Group ID are optional. "+
			"Each region needs to be modified using a separate --edit-region flag.")
	updateAzureProviderCmd.Flags().StringArray("edit-zone", []string{},
		"[Optional] Edit zone associated to the Azure Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>::"+
			"secondary-subnet=<secondary-subnet-id>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Subnet IDs and Secondary subnet ID is optional. "+
			"Each zone needs to be modified using a separate --edit-zone flag.")

	updateAzureProviderCmd.Flags().StringArray("add-image-bundle", []string{},
		"[Optional] Add Intel x86_64 image bundles associated with the provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>::machine-image=<custom-ami>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle name, machine image and SSH user are required key-value pairs.",
				formatter.GreenColor)+
			" The default SSH Port is 22. Default marks the image bundle as default for the provider. "+
			"If default is not specified, the bundle will automatically be set as default "+
			"if no other default bundle exists for the same architecture. "+
			"Each image bundle can be added using separate --add-image-bundle flag.")

	updateAzureProviderCmd.Flags().StringArray("edit-image-bundle", []string{},
		"[Optional] Edit Intel x86_64 image bundles associated with the provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-uuid=<image-bundle-uuid>::machine-image=<custom-ami>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle UUID is a required key-value pair.",
				formatter.GreenColor)+
			"Each image bundle can be added using separate --edit-image-bundle flag.")

	updateAzureProviderCmd.Flags().StringArray("remove-image-bundle", []string{},
		"[Optional] Image bundle UUID to be removed from the provider. "+
			"Each bundle to be removed needs to be provided using a separate "+
			"--remove-image-bundle definition.")

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

func removeAzureImageBundles(
	removeImageBundles []string,
	providerImageBundles []ybaclient.ImageBundle) []ybaclient.ImageBundle {
	if len(removeImageBundles) == 0 {
		return providerImageBundles
	}

	for _, ib := range removeImageBundles {
		for i, pIb := range providerImageBundles {
			if strings.Compare(pIb.GetUuid(), ib) == 0 {
				providerImageBundles = util.RemoveComponentFromSlice(
					providerImageBundles, i,
				).([]ybaclient.ImageBundle)
			}
		}
	}

	return providerImageBundles
}

func editAzureImageBundles(
	editImageBundles []string,
	providerImageBundles []ybaclient.ImageBundle,
) []ybaclient.ImageBundle {

	for i, ib := range providerImageBundles {
		bundleUUID := ib.GetUuid()
		details := ib.GetDetails()
		if len(editImageBundles) != 0 {
			for _, imageBundleString := range editImageBundles {
				imageBundle := providerutil.BuildImageBundleMapFromString(imageBundleString, "edit")

				if strings.Compare(imageBundle["uuid"], bundleUUID) == 0 {
					if len(imageBundle["machine-image"]) != 0 {
						details.SetGlobalYbImage(imageBundle["machine-image"])
					}
					if len(imageBundle["ssh-user"]) != 0 {
						details.SetSshUser(imageBundle["ssh-user"])
					}
					if len(imageBundle["ssh-port"]) != 0 {
						sshPort, err := strconv.ParseInt(imageBundle["ssh-port"], 10, 64)
						if err != nil {
							errMessage := err.Error() +
								" Invalid or missing value provided for 'ssh-port'. Setting it to '22'.\n"
							logrus.Errorln(
								formatter.Colorize(errMessage, formatter.YellowColor),
							)
							sshPort = 22
						}
						details.SetSshPort(int32(sshPort))
					}

					ib.SetDetails(details)

					if len(imageBundle["default"]) != 0 {
						defaultBundle, err := strconv.ParseBool(imageBundle["default"])
						if err != nil {
							errMessage := err.Error() +
								" Invalid or missing value provided for 'default'. Setting it to 'false'.\n"
							logrus.Errorln(
								formatter.Colorize(errMessage, formatter.YellowColor),
							)
							defaultBundle = false
						}
						ib.SetUseAsDefault(defaultBundle)
					}

				}

			}
		}
		providerImageBundles[i] = ib
	}
	return providerImageBundles
}

func addAzureImageBundles(
	imageBundles []string,
	providerImageBundles []ybaclient.ImageBundle,
) []ybaclient.ImageBundle {
	if len(imageBundles) == 0 {
		return providerImageBundles
	}
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

		// If default not explicitly set, check if there's already a default bundle
		// for this architecture. If not, set this bundle as default.
		if !defaultBundle {
			arch := strings.ToLower(bundle["arch"])
			hasDefaultForArch := false
			for _, existingBundle := range providerImageBundles {
				existingArch := strings.ToLower(existingBundle.Details.GetArch())
				if existingArch == arch && existingBundle.GetUseAsDefault() {
					hasDefaultForArch = true
					break
				}
			}
			if !hasDefaultForArch {
				defaultBundle = true
			}
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
		providerImageBundles = append(providerImageBundles, imageBundle)
	}
	return providerImageBundles
}
