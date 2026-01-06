/*
 * Copyright (c) YugabyteDB, Inc.
 */

package aws

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

// updateAWSProviderCmd represents the provider command
var updateAWSProviderCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an AWS YugabyteDB Anywhere provider",
	Long:    "Update an AWS provider in YugabyteDB Anywhere",
	Example: `yba provider aws update --name <provider-name> \
	 --remove-region <region-1> --remove-region <region-2>`,
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
		var isIAM bool
		iamFlagSet := cmd.Flags().Changed("use-iam-instance-profile")
		if iamFlagSet {
			isIAM, err = cmd.Flags().GetBool("use-iam-instance-profile")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		accessKeyID, err := cmd.Flags().GetString("access-key-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if isIAM && len(accessKeyID) > 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Cannot set both credentials and use-iam-instance-profile"+
					"\n", formatter.RedColor))
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
			util.FatalHTTPError(response, err, "Provider: AWS", "Update - Fetch Providers")
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		var provider ybaclient.Provider
		providerCode := util.AWSProviderType
		for _, p := range r {
			if p.GetCode() == providerCode {
				provider = p
			}
		}

		if util.IsEmptyString(provider.GetName()) {
			errMessage := fmt.Sprintf(
				"No provider %s in cloud type %s.",
				providerName,
				providerCode,
			)
			logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
		}

		providerRegions := provider.GetRegions()
		details := provider.GetDetails()
		cloudInfo := details.GetCloudInfo()
		awsCloudInfo := cloudInfo.GetAws()

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

		hostedZoneID, err := cmd.Flags().GetString("hosted-zone-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(hostedZoneID) > 0 {
			logrus.Debug("Updating Hosted Zone ID\n")
			awsCloudInfo.SetAwsHostedZoneId(hostedZoneID)
		}

		accessKeyID, err := cmd.Flags().GetString("access-key-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		secretAccessKey, err := cmd.Flags().GetString("secret-access-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(accessKeyID) &&
			!util.IsEmptyString(secretAccessKey) {
			logrus.Debug("Updating AWS credentials\n")
			awsCloudInfo.SetAwsAccessKeyID(accessKeyID)
			awsCloudInfo.SetAwsAccessKeySecret(secretAccessKey)
		}

		isIAMFlagSet := cmd.Flags().Changed("use-iam-instance-profile")
		if isIAMFlagSet {
			isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if isIAM {
				logrus.Debug("Updating provider to use IAM instance profile\n")
				awsCloudInfo.SetAwsAccessKeyID("")
				awsCloudInfo.SetAwsAccessKeySecret("")
			}
		}
		cloudInfo.SetAws(awsCloudInfo)
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

		providerRegions = removeAWSRegions(removeRegions, providerRegions)

		providerRegions = editAWSRegions(
			editRegions,
			addZones,
			editZones,
			removeZones,
			providerRegions,
		)

		providerRegions = addAWSRegions(addRegions, addZones, providerRegions)

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

		editImageBundleRegionOverride, err := cmd.Flags().GetStringArray(
			"edit-image-bundle-region-override")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		addImageBundleRegionOverride, err := cmd.Flags().GetStringArray(
			"add-image-bundle-region-override")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerImageBundles = removeAWSImageBundles(removeImageBundles, providerImageBundles)

		providerImageBundles = editAWSImageBundles(
			editImageBundles,
			editImageBundleRegionOverride,
			providerImageBundles)

		providerImageBundles = addAWSImageBundles(
			addImageBundles,
			addImageBundleRegionOverride,
			providerImageBundles,
			len(providerRegions),
		)

		provider.SetImageBundles(providerImageBundles)

		// End of Updating Image Bundles

		rTask, response, err := authAPI.EditProvider(provider.GetUuid()).
			EditProviderRequest(provider).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Provider: AWS", "Update")
		}

		providerutil.WaitForUpdateProviderTask(
			authAPI, providerName, rTask, providerCode)
	},
}

func init() {
	updateAWSProviderCmd.Flags().SortFlags = false

	updateAWSProviderCmd.Flags().String("new-name", "",
		"[Optional] Updating provider name.")

	updateAWSProviderCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("[Optional] AWS Access Key ID. %s ",
			formatter.Colorize("Required if"+
				" provider does not use IAM instance profile."+
				" Required with secret-access-key.",
				formatter.GreenColor)))
	updateAWSProviderCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("[Optional] AWS Secret Access Key. "+
			formatter.Colorize("Required if"+
				" provider does not use IAM instance profile."+
				" Required with access-key-id.",
				formatter.GreenColor)))
	updateAWSProviderCmd.MarkFlagsRequiredTogether("access-key-id", "secret-access-key")

	updateAWSProviderCmd.Flags().Bool("use-iam-instance-profile", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host.")

	updateAWSProviderCmd.Flags().String("hosted-zone-id", "",
		"[Optional] Updating Hosted Zone ID corresponding to Amazon Route53.")

	updateAWSProviderCmd.Flags().StringArray("add-region", []string{},
		"[Optional] Add region associated with the AWS provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs:"+
			"\"region-name=<region-name>::"+
			"vpc-id=<vpc-id>::sg-id=<security-group-id>\". "+
			formatter.Colorize("Region name is required key-value.",
				formatter.GreenColor)+
			" VPC ID and Security Group ID are optional. "+
			"Each region needs to be added using a separate --add-region flag.")
	updateAWSProviderCmd.Flags().StringArray("add-zone", []string{},
		"[Optional] Zone associated to the AWS Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>::"+
			"secondary-subnet=<secondary-subnet-id>\". "+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Secondary subnet ID is optional. Each --add-region definition "+
			"must have atleast one corresponding --add-zone definition. "+
			"Multiple --add-zone definitions "+
			"can be provided for new and existing regions."+
			"Each zone needs to be added using a separate --add-zone flag.")

	updateAWSProviderCmd.Flags().StringArray("remove-region", []string{},
		"[Optional] Region name to be removed from the provider. "+
			"Each region to be removed needs to be provided using a separate "+
			"--remove-region definition. Removing a region removes the corresponding zones.")
	updateAWSProviderCmd.Flags().StringArray("remove-zone", []string{},
		"[Optional] Remove zone associated to the AWS Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>::region-name=<region-name>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Each zone needs to be removed using a separate --remove-zone flag.")

	updateAWSProviderCmd.Flags().StringArray("edit-region", []string{},
		"[Optional] Edit region details associated with the AWS provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs:"+
			"\"region-name=<region-name>::"+
			"vpc-id=<vpc-id>::sg-id=<security-group-id>\". "+
			formatter.Colorize("Region name is a required key-value pair.",
				formatter.GreenColor)+
			" VPC ID and Security Group ID are optional. "+
			"Each region needs to be modified using a separate --edit-region flag.")
	updateAWSProviderCmd.Flags().StringArray("edit-zone", []string{},
		"[Optional] Edit zone associated to the AWS Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>::"+
			"secondary-subnet=<secondary-subnet-id>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Subnet IDs and Secondary subnet ID is optional. "+
			"Each zone needs to be modified using a separate --edit-zone flag.")

	updateAWSProviderCmd.Flags().StringArray("add-image-bundle", []string{},
		"[Optional] Add Image bundles associated with the provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>::arch=<architecture>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::imdsv2=<true/false>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle name, architecture and SSH user are required key-value pairs.",
				formatter.GreenColor)+
			" The default for SSH Port is 22, IMDSv2 ("+
			"This should be true if the Image bundle requires Instance Metadata Service v2)"+
			" is false. Default marks the image bundle as default for the provider. "+
			"If default is not specified, the bundle will automatically be set as default "+
			"if no other default bundle exists for the same architecture. "+
			"Allowed values for architecture are x86_64 and arm64/aarch64. "+
			"Each image bundle can be added using separate --add-image-bundle flag.")
	updateAWSProviderCmd.Flags().StringArray("add-image-bundle-region-override", []string{},
		"[Optional] Add Image bundle region overrides associated with the provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>::region-name=<region-name>::"+
			"machine-image=<machine-image>\". "+
			formatter.Colorize(
				"Image bundle name and region name are required key-value pairs.",
				formatter.GreenColor)+" Each --image-bundle definition "+
			"must have atleast one corresponding --image-bundle-region-override "+
			"definition for every region added."+
			" Each override can be added using separate --add-image-bundle-region-override flag.")

	updateAWSProviderCmd.Flags().StringArray("edit-image-bundle", []string{},
		"[Optional] Edit Image bundles associated with the provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-uuid=<image-bundle-uuid>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::imdsv2=<true/false>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle UUID is a required key-value pair.",
				formatter.GreenColor)+
			" Each image bundle can be edited using separate --edit-image-bundle flag.")
	updateAWSProviderCmd.Flags().StringArray("edit-image-bundle-region-override", []string{},
		"[Optional] Edit overrides of the region associated with the provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-uuid=<image-bundle-uuid>::region-name=<region-name>::"+
			"machine-image=<machine-image>\". "+
			formatter.Colorize(
				"Image bundle UUID and region name are required key-value pairs.",
				formatter.GreenColor)+
			" Each image bundle can be added using separate --edit-image-bundle-region-override flag.")

	updateAWSProviderCmd.Flags().StringArray("remove-image-bundle", []string{},
		"[Optional] Image bundle UUID to be removed from the provider. "+
			"Each bundle to be removed needs to be provided using a separate "+
			"--remove-image-bundle definition. "+
			"Removing a image bundle removes the corresponding region overrides.")

	updateAWSProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads. "+
			"(default false)")
	updateAWSProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")

}

func removeAWSRegions(
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

func editAWSRegions(
	editRegions, addZones, editZones, removeZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {

	for i, r := range providerRegions {
		regionName := r.GetCode()
		zones := r.GetZones()
		zones = removeAWSZones(regionName, removeZones, zones)
		zones = editAWSZones(regionName, editZones, zones)
		zones = addAWSZones(regionName, addZones, zones)
		r.SetZones(zones)
		if len(editRegions) != 0 {
			for _, regionString := range editRegions {
				region := providerutil.BuildRegionMapFromString(regionString, "edit")

				if strings.Compare(region["name"], regionName) == 0 {
					details := r.GetDetails()
					cloudInfo := details.GetCloudInfo()
					aws := cloudInfo.GetAws()
					if len(region["vpc-id"]) != 0 {
						aws.SetVnet(region["vpc-id"])
					}
					if len(region["sg-id"]) != 0 {
						aws.SetSecurityGroupId(region["sg-id"])
					}
					cloudInfo.SetAws(aws)
					details.SetCloudInfo(cloudInfo)
					r.SetDetails(details)

				}

			}
		}
		providerRegions[i] = r
	}
	return providerRegions
}

func addAWSRegions(
	addRegions, addZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {
	if len(addRegions) == 0 {
		return providerRegions
	}
	for _, regionString := range addRegions {
		region := providerutil.BuildRegionMapFromString(regionString, "add")

		zones := addAWSZones(region["name"], addZones, make([]ybaclient.AvailabilityZone, 0))
		r := ybaclient.Region{
			Code:  util.GetStringPointer(region["name"]),
			Name:  util.GetStringPointer(region["name"]),
			Zones: zones,
			Details: &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Aws: &ybaclient.AWSRegionCloudInfo{
						SecurityGroupId: util.GetStringPointer(region["sg-id"]),
						Vnet:            util.GetStringPointer(region["vpc-id"]),
					},
				},
			},
		}

		providerRegions = append(providerRegions, r)
	}

	return providerRegions
}

func removeAWSZones(
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

func editAWSZones(
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

func addAWSZones(
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

func removeAWSImageBundles(
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

func editAWSImageBundles(
	editImageBundles, editImageBundlesRegionOverrides []string,
	providerImageBundles []ybaclient.ImageBundle,
) []ybaclient.ImageBundle {

	for i, ib := range providerImageBundles {
		bundleUUID := ib.GetUuid()
		details := ib.GetDetails()
		imageBundleRegionOverrides := details.GetRegions()
		imageBundleRegionOverrides = editAWSImageBundleRegionOverrides(
			bundleUUID,
			editImageBundlesRegionOverrides,
			imageBundleRegionOverrides)
		details.SetRegions(imageBundleRegionOverrides)
		if len(editImageBundles) != 0 {
			for _, imageBundleString := range editImageBundles {
				imageBundle := providerutil.BuildImageBundleMapFromString(
					imageBundleString,
					"edit",
				)

				if strings.Compare(imageBundle["uuid"], bundleUUID) == 0 {

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
					if len(imageBundle["imdsv2"]) != 0 {
						useIMDSv2, err := strconv.ParseBool(imageBundle["imdsv2"])
						if err != nil {
							errMessage := err.Error() +
								" Invalid or missing value provided for 'imdsv2'. Setting it to 'false'.\n"
							logrus.Errorln(
								formatter.Colorize(errMessage, formatter.YellowColor),
							)
							useIMDSv2 = false
						}
						details.SetUseIMDSv2(useIMDSv2)
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

func editAWSImageBundleRegionOverrides(
	bundleUUID string,
	editImageBundleRegionOverrides []string,
	imageBundleRegionOverrides map[string]ybaclient.BundleInfo,
) map[string]ybaclient.BundleInfo {

	if len(editImageBundleRegionOverrides) == 0 {
		return imageBundleRegionOverrides
	}

	for _, imageBundleString := range editImageBundleRegionOverrides {
		override := providerutil.BuildImageBundleRegionOverrideMapFromString(
			imageBundleString,
			"edit",
		)
		if strings.Compare(override["uuid"], bundleUUID) == 0 {
			for k, v := range override {
				if _, ok := imageBundleRegionOverrides[k]; ok {
					imageBundleRegionOverrides[k] = ybaclient.BundleInfo{
						YbImage: util.GetStringPointer(v),
					}
				}
			}
		}

	}

	return imageBundleRegionOverrides
}

func addAWSImageBundles(
	addImageBundles,
	addImageBundleRegionOverrides []string,
	providerImageBundles []ybaclient.ImageBundle,
	numberOfRegions int) []ybaclient.ImageBundle {
	if len(addImageBundles) == 0 {
		return providerImageBundles
	}
	for _, i := range addImageBundles {
		bundle := providerutil.BuildImageBundleMapFromString(i, "add")

		regionOverrides := buildAWSImageBundleRegionOverrides(
			addImageBundleRegionOverrides,
			bundle["name"])

		if len(regionOverrides) < numberOfRegions {
			logrus.Fatalf(formatter.Colorize(
				"Overrides must be provided for every region added.\n",
				formatter.RedColor,
			))
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

		useIMDSv2, err := strconv.ParseBool(bundle["imdsv2"])
		if err != nil {
			errMessage := err.Error() +
				" Invalid or missing value provided for 'imdsv2'. Setting it to 'false'.\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			useIMDSv2 = false
		}

		imageBundle := ybaclient.ImageBundle{
			Name:         util.GetStringPointer(bundle["name"]),
			UseAsDefault: util.GetBoolPointer(defaultBundle),
			Details: &ybaclient.ImageBundleDetails{
				Arch:      util.GetStringPointer(strings.ToLower(bundle["arch"])),
				SshUser:   util.GetStringPointer(bundle["ssh-user"]),
				SshPort:   util.GetInt32Pointer(int32(sshPort)),
				UseIMDSv2: util.GetBoolPointer(useIMDSv2),
				Regions:   &regionOverrides,
			},
		}
		providerImageBundles = append(providerImageBundles, imageBundle)
	}
	return providerImageBundles
}
