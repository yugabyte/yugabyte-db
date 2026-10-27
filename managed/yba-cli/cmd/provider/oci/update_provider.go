/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"fmt"
	"slices"
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

// updateOCIProviderCmd represents the provider command
var updateOCIProviderCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an OCI YugabyteDB Anywhere provider",
	Long:    "Update an OCI provider in YugabyteDB Anywhere",
	Example: `yba provider oci update --name <provider-name> \
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
			util.FatalHTTPError(response, err, "Provider: OCI", "Update - Fetch Providers")
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		var provider ybaclient.Provider
		providerCode := util.OCIProviderType
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
		ociCloudInfo := cloudInfo.GetOci()

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

		currentAuthType, err := util.NormalizeOCIAuthType(ociCloudInfo.GetOciAuthType())
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authType, err := cmd.Flags().GetString("auth-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(authType) > 0 {
			authType, err = util.NormalizeOCIAuthType(authType)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		} else {
			authType = currentAuthType
		}

		tenancyID, userID, fingerprint, keyPath := ociAPIKeyFlags(cmd)
		apiKeyFlagsSet := len(tenancyID) > 0 || len(userID) > 0 ||
			len(fingerprint) > 0 || len(keyPath) > 0

		switch authType {
		case util.OCIAuthTypeInstancePrincipal:
			if apiKeyFlagsSet {
				logrus.Fatalln(formatter.Colorize(
					fmt.Sprintf("API key flags cannot be set on a provider that uses %s auth\n",
						util.OCIAuthTypeInstancePrincipal),
					formatter.RedColor))
			}
			if currentAuthType != authType {
				logrus.Debug("Clearing OCI API key credentials\n")
				ociCloudInfo.OciTenancyId = nil
				ociCloudInfo.OciUserId = nil
				ociCloudInfo.OciFingerprint = nil
				ociCloudInfo.OciPrivateKeyContent = nil
			}
		case util.OCIAuthTypeAPIKey:
			if currentAuthType != authType && !apiKeyFlagsSet {
				logrus.Fatalln(formatter.Colorize(
					fmt.Sprintf("Switching to %s auth requires --tenancy-id, --user-id, "+
						"--fingerprint and --private-key-file-path\n", util.OCIAuthTypeAPIKey),
					formatter.RedColor))
			}
			if apiKeyFlagsSet {
				privateKey, err := util.ReadFileToString(keyPath)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				logrus.Debug("Updating OCI API key credentials\n")
				ociCloudInfo.SetOciTenancyId(tenancyID)
				ociCloudInfo.SetOciUserId(userID)
				ociCloudInfo.SetOciFingerprint(fingerprint)
				ociCloudInfo.SetOciPrivateKeyContent(*privateKey)
			}
		}
		if currentAuthType != authType {
			logrus.Debug("Updating OCI Auth Type\n")
			ociCloudInfo.SetOciAuthType(authType)
		}

		compartmentID, err := cmd.Flags().GetString("compartment-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(compartmentID) > 0 {
			logrus.Debug("Updating OCI Compartment ID\n")
			ociCloudInfo.SetOciCompartmentId(compartmentID)
		}

		ociRegion, err := cmd.Flags().GetString("oci-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(ociRegion) > 0 {
			logrus.Debug("Updating OCI Region\n")
			ociCloudInfo.SetOciRegion(ociRegion)
		}

		hostedZoneID, err := cmd.Flags().GetString("hosted-zone-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(hostedZoneID) > 0 {
			logrus.Debug("Updating Hosted Zone ID\n")
			ociCloudInfo.SetOciHostedZoneId(hostedZoneID)
		}

		cloudInfo.SetOci(ociCloudInfo)
		details.SetCloudInfo(cloudInfo)

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

		providerRegions = removeOCIRegions(removeRegions, providerRegions)
		providerRegions = editOCIRegions(
			editRegions,
			addZones,
			editZones,
			removeZones,
			providerRegions,
		)
		providerRegions = addOCIRegions(addRegions, addZones, providerRegions)
		provider.SetRegions(providerRegions)

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
		addImageBundleRegionOverrides, err := cmd.Flags().GetStringArray(
			"add-image-bundle-region-override")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		editImageBundleRegionOverrides, err := cmd.Flags().GetStringArray(
			"edit-image-bundle-region-override")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		regionCodes := ociRegionCodes(providerRegions)
		providerImageBundles = removeOCIImageBundles(removeImageBundles, providerImageBundles)
		providerImageBundles, err = editOCIImageBundles(
			editImageBundles,
			providerImageBundles,
			regionCodes,
			editImageBundleRegionOverrides,
		)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		providerImageBundles, err = addOCIImageBundles(
			addImageBundles,
			providerImageBundles,
			regionCodes,
			addImageBundleRegionOverrides,
		)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		provider.SetImageBundles(providerImageBundles)

		rTask, response, err := authAPI.EditProvider(provider.GetUuid()).
			EditProviderRequest(provider).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Provider: OCI", "Update")
		}

		providerutil.WaitForUpdateProviderTask(
			authAPI, providerName, rTask, providerCode)
	},
}

func init() {
	updateOCIProviderCmd.Flags().SortFlags = false

	updateOCIProviderCmd.Flags().String("new-name", "",
		"[Optional] Updating provider name.")

	updateOCIProviderCmd.Flags().String("auth-type", "",
		fmt.Sprintf("[Optional] Update OCI authentication type. Allowed values: %s, %s. "+
			"Switching to %s requires the API key flags. Switching to %s "+
			"clears the stored API key credentials.",
			util.OCIAuthTypeAPIKey, util.OCIAuthTypeInstancePrincipal,
			util.OCIAuthTypeAPIKey, util.OCIAuthTypeInstancePrincipal))
	updateOCIProviderCmd.Flags().String("tenancy-id", "",
		"[Optional] Update OCI Tenancy OCID. Must be set with --user-id, "+
			"--fingerprint and --private-key-file-path.")
	updateOCIProviderCmd.Flags().String("user-id", "",
		"[Optional] Update OCI User OCID. Must be set with --tenancy-id, "+
			"--fingerprint and --private-key-file-path.")
	updateOCIProviderCmd.Flags().String("fingerprint", "",
		"[Optional] Update OCI API key fingerprint. Must be set with --tenancy-id, "+
			"--user-id and --private-key-file-path.")
	updateOCIProviderCmd.Flags().String("private-key-file-path", "",
		"[Optional] Update OCI API private key from a PEM file. Must be set with "+
			"--tenancy-id, --user-id and --fingerprint.")
	updateOCIProviderCmd.MarkFlagsRequiredTogether(
		"tenancy-id", "user-id", "fingerprint", "private-key-file-path")
	updateOCIProviderCmd.Flags().String("compartment-id", "",
		"[Optional] Update OCI Compartment OCID.")
	updateOCIProviderCmd.Flags().String("oci-region", "",
		"[Optional] Update the default OCI region.")
	updateOCIProviderCmd.Flags().String("hosted-zone-id", "",
		"[Optional] Update OCI DNS Zone OCID.")

	updateOCIProviderCmd.Flags().StringArray("add-region", []string{},
		"[Optional] Add region associated with the OCI provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"region-name=<region-name>::vnet=<vcn-ocid>::instance-template=<instance-config-ocid>\". "+
			formatter.Colorize("Region name and Virtual network are required key-values.",
				formatter.GreenColor)+
			" Instance template is optional. "+
			"Each region needs to be added using a separate --add-region flag.")
	updateOCIProviderCmd.Flags().StringArray("add-zone", []string{},
		"[Optional] Zone associated to the OCI Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-ocid>\"."+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Each --add-region definition "+
			"must have atleast one corresponding --add-zone definition. Multiple"+
			" --add-zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --add-zone flag.")

	updateOCIProviderCmd.Flags().StringArray("remove-region", []string{},
		"[Optional] Region name to be removed from the provider. "+
			"Each region to be removed needs to be provided using a separate "+
			"--remove-region definition. Removing a region removes the corresponding zones.")
	updateOCIProviderCmd.Flags().StringArray("remove-zone", []string{},
		"[Optional] Remove zone associated to the OCI Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"zone-name=<zone-name>::region-name=<region-name>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Each zone needs to be removed using a separate --remove-zone flag.")

	updateOCIProviderCmd.Flags().StringArray("edit-region", []string{},
		"[Optional] Edit region details associated with the OCI provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"region-name=<region-name>::vnet=<vcn-ocid>::instance-template=<instance-config-ocid>\". "+
			formatter.Colorize("Region name is a required key-value pair.",
				formatter.GreenColor)+
			" Virtual network and instance template are optional. "+
			"Each region needs to be modified using a separate --edit-region flag.")
	updateOCIProviderCmd.Flags().StringArray("edit-zone", []string{},
		"[Optional] Edit zone associated to the OCI Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-ocid>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Subnet ID is optional. "+
			"Each zone needs to be modified using a separate --edit-zone flag.")

	updateOCIProviderCmd.Flags().StringArray("add-image-bundle", []string{},
		"[Optional] Add image bundles associated with the provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>::arch=<architecture>::"+
			"machine-image=<image-ocid>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle name and SSH user are required key-value pairs.",
				formatter.GreenColor)+
			" machine-image is the image OCID when the provider has one region. "+
			"OCI image OCIDs are region scoped, so a provider with more than one region "+
			"needs --add-image-bundle-region-override once per region instead of machine-image. "+
			fmt.Sprintf(
				"Allowed values for architecture are %s and %s, defaults to %s.",
				util.X86_64, util.AARCH64, util.X86_64)+
			" The default SSH Port is 22. Default marks the image bundle as default for the provider. "+
			"If default is not specified, the bundle will automatically be set as default "+
			"if no other default bundle exists for the same architecture. "+
			"Each image bundle can be added using separate --add-image-bundle flag.")
	updateOCIProviderCmd.Flags().StringArray("add-image-bundle-region-override", []string{},
		"[Optional] Per-region OCI image for a bundle added with --add-image-bundle. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>::region-name=<region-name>::"+
			"machine-image=<image-ocid>\". "+
			formatter.Colorize(
				"Image bundle name, region name, and machine image are required key-value pairs.",
				formatter.GreenColor)+
			" Required once per provider region when the provider has more than one region. "+
			"Each override needs a separate --add-image-bundle-region-override flag.")

	updateOCIProviderCmd.Flags().StringArray("edit-image-bundle", []string{},
		"[Optional] Edit image bundles associated with the provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-uuid=<image-bundle-uuid>::machine-image=<image-ocid>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle UUID is a required key-value pair.",
				formatter.GreenColor)+
			" machine-image updates the image when the provider has one region. "+
			"A provider with more than one region uses --edit-image-bundle-region-override. "+
			"Each image bundle can be edited using separate --edit-image-bundle flag.")
	updateOCIProviderCmd.Flags().StringArray("edit-image-bundle-region-override", []string{},
		"[Optional] Edit the image OCID for one region of an OCI image bundle. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-uuid=<image-bundle-uuid>::region-name=<region-name>::"+
			"machine-image=<image-ocid>\". "+
			formatter.Colorize(
				"Image bundle UUID, region name, and machine image are required key-value pairs.",
				formatter.GreenColor)+
			" Each region needs a separate --edit-image-bundle-region-override flag.")

	updateOCIProviderCmd.Flags().StringArray("remove-image-bundle", []string{},
		"[Optional] Image bundle UUID to be removed from the provider. "+
			"Each bundle to be removed needs to be provided using a separate "+
			"--remove-image-bundle definition.")

	updateOCIProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads. "+
			"(default false)")
	updateOCIProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")
}

func removeOCIRegions(
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

func editOCIRegions(
	editRegions, addZones, editZones, removeZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {
	for i, r := range providerRegions {
		regionName := r.GetCode()
		zones := r.GetZones()
		zones = removeOCIZones(regionName, removeZones, zones)
		zones = editOCIZones(regionName, editZones, zones)
		zones = addOCIZones(regionName, addZones, zones)
		r.SetZones(zones)
		if len(editRegions) != 0 {
			for _, regionString := range editRegions {
				region := providerutil.BuildRegionMapFromString(regionString, "edit")
				if strings.Compare(region["name"], regionName) == 0 {
					details := r.GetDetails()
					cloudInfo := details.GetCloudInfo()
					ociRegion := cloudInfo.GetOci()
					if len(region["vnet"]) != 0 {
						ociRegion.SetVnet(region["vnet"])
					}
					if len(region["instance-template"]) != 0 {
						ociRegion.SetInstanceTemplate(region["instance-template"])
					}
					cloudInfo.SetOci(ociRegion)
					details.SetCloudInfo(cloudInfo)
					r.SetDetails(details)
				}
			}
		}
		providerRegions[i] = r
	}
	return providerRegions
}

func addOCIRegions(
	addRegions, addZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {
	if len(addRegions) == 0 {
		return providerRegions
	}
	for _, regionString := range addRegions {
		region := providerutil.BuildRegionMapFromString(regionString, "add")
		if _, ok := region["vnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Virtual Network not specified in region info.\n",
					formatter.RedColor))
		}
		zones := addOCIZones(region["name"], addZones, make([]ybaclient.AvailabilityZone, 0))
		r := ybaclient.Region{
			Code:  util.GetStringPointer(region["name"]),
			Name:  util.GetStringPointer(region["name"]),
			Zones: zones,
			Details: &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Oci: &ybaclient.OCIRegionCloudInfo{
						Vnet:             util.GetStringPointer(region["vnet"]),
						InstanceTemplate: util.GetStringPointer(region["instance-template"]),
					},
				},
			},
		}
		providerRegions = append(providerRegions, r)
	}
	return providerRegions
}

func removeOCIZones(
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

func editOCIZones(
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

func addOCIZones(
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
				Code:   util.GetStringPointer(zone["name"]),
				Name:   zone["name"],
				Subnet: util.GetStringPointer(zone["subnet"]),
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

func removeOCIImageBundles(
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

// applyOCIRegionImageEdit updates per-region images. machineImage applies only
// when the provider has one region. overrides update the named regions.
func applyOCIRegionImageEdit(
	details *ybaclient.ImageBundleDetails,
	regionCodes []string,
	machineImage string,
	overrides map[string]string,
) error {
	if machineImage == "" && len(overrides) == 0 {
		return nil
	}
	if len(regionCodes) == 0 {
		return fmt.Errorf("at least one region is required for an OCI image bundle")
	}
	if machineImage != "" && len(overrides) > 0 {
		return fmt.Errorf("machine-image cannot be combined with image-bundle region overrides")
	}

	regions := details.GetRegions()
	if regions == nil {
		regions = map[string]ybaclient.BundleInfo{}
	}
	if len(overrides) == 0 {
		if len(regionCodes) != 1 {
			return fmt.Errorf(
				"OCI image OCIDs are region scoped; provide an image-bundle region override for the region to update",
			)
		}
		info := regions[regionCodes[0]]
		info.SetYbImage(machineImage)
		regions[regionCodes[0]] = info
	} else {
		for code, image := range overrides {
			if !slices.Contains(regionCodes, code) {
				return fmt.Errorf("region %s is not configured on the provider", code)
			}
			if image == "" {
				return fmt.Errorf("machine image not specified for region %s", code)
			}
			info := regions[code]
			info.SetYbImage(image)
			regions[code] = info
		}
	}
	details.SetRegions(regions)
	return nil
}

func editOCIImageBundles(
	editImageBundles []string,
	providerImageBundles []ybaclient.ImageBundle,
	regionCodes []string,
	regionOverrides []string,
) ([]ybaclient.ImageBundle, error) {
	overridesByBundle := ociOverridesByBundle(regionOverrides, "uuid")
	for i, ib := range providerImageBundles {
		bundleUUID := ib.GetUuid()
		details := ib.GetDetails()
		machineImage := ""
		if len(editImageBundles) != 0 {
			for _, imageBundleString := range editImageBundles {
				imageBundle := providerutil.BuildImageBundleMapFromString(imageBundleString, "edit")
				if strings.Compare(imageBundle["uuid"], bundleUUID) == 0 {
					machineImage = imageBundle["machine-image"]
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
		if err := applyOCIRegionImageEdit(
			&details,
			regionCodes,
			machineImage,
			overridesByBundle[bundleUUID],
		); err != nil {
			return nil, err
		}
		delete(overridesByBundle, bundleUUID)
		ib.SetDetails(details)
		providerImageBundles[i] = ib
	}
	if err := unmatchedOverridesError(overridesByBundle); err != nil {
		return nil, err
	}
	return providerImageBundles, nil
}

func addOCIImageBundles(
	imageBundles []string,
	providerImageBundles []ybaclient.ImageBundle,
	regionCodes []string,
	regionOverrides []string,
) ([]ybaclient.ImageBundle, error) {
	overridesByBundle := ociOverridesByBundle(regionOverrides, "name")
	bundles := make([]map[string]string, 0, len(imageBundles))
	// Auto-promoting a bundle on an arch that also has an explicit default
	// fails server side with two defaults for that arch.
	explicitDefaultArchs := map[string]bool{}
	for _, i := range imageBundles {
		bundle := providerutil.DefaultImageBundleValues(
			providerutil.BuildImageBundleMapFromString(i, "add"))
		if isDefault, _ := strconv.ParseBool(bundle["default"]); isDefault {
			explicitDefaultArchs[strings.ToLower(bundle["arch"])] = true
		}
		bundles = append(bundles, bundle)
	}
	for _, bundle := range bundles {
		if _, ok := bundle["ssh-user"]; !ok {
			return nil, fmt.Errorf("SSH User not specified in image bundle")
		}

		regions, err := ociBundleRegions(
			regionCodes,
			bundle["machine-image"],
			overridesByBundle[bundle["name"]],
		)
		if err != nil {
			return nil, err
		}
		delete(overridesByBundle, bundle["name"])

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

		if !defaultBundle {
			arch := strings.ToLower(bundle["arch"])
			hasDefaultForArch := explicitDefaultArchs[arch]
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
				Arch:    util.GetStringPointer(bundle["arch"]),
				SshUser: util.GetStringPointer(bundle["ssh-user"]),
				SshPort: util.GetInt32Pointer(int32(sshPort)),
				Regions: &regions,
			},
		}
		providerImageBundles = append(providerImageBundles, imageBundle)
	}
	if err := unmatchedOverridesError(overridesByBundle); err != nil {
		return nil, err
	}
	return providerImageBundles, nil
}
