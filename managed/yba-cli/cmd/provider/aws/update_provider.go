/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

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

// updateAWSProviderCmd represents the provider command
var updateAWSProviderCmd = &cobra.Command{
	Use:   "update",
	Short: "Update an AWS YugabyteDB Anywhere provider",
	Long:  "Update an AWS provider in YugabyteDB Anywhere",
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
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Provider: AWS", "Update - Fetch Providers")
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
		providerCode := util.AWSProviderType
		for _, p := range r {
			if p.GetCode() == providerCode {
				provider = p
			}
		}

		if len(strings.TrimSpace(provider.GetName())) == 0 {
			errMessage := fmt.Sprintf("No provider %s in cloud type %s.", providerName, providerCode)
			logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
		}

		providerRegions := provider.GetRegions()
		details := provider.GetDetails()
		cloudInfo := details.GetCloudInfo()
		awsCloudInfo := cloudInfo.GetAws()

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
		if len(strings.TrimSpace(accessKeyID)) != 0 && len(strings.TrimSpace(secretAccessKey)) != 0 {
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

		providerRegions = removeAWSRegions(removeRegions, providerRegions)

		providerRegions = editAWSRegions(editRegions, addZones, editZones, removeZones, providerRegions)

		providerRegions = addAWSRegions(addRegions, addZones, providerRegions)

		provider.SetRegions(providerRegions)
		// End of Updating Regions

		rUpdate, response, err := authAPI.EditProvider(provider.GetUuid()).
			EditProviderRequest(provider).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider: AWS", "Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rUpdate.GetResourceUUID()
		taskUUID := rUpdate.GetTaskUUID()

		providerutil.WaitForUpdateProviderTask(
			authAPI, providerName, providerUUID, providerCode, taskUUID)
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
			"Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,"+
			"vpc-id=<vpc-id>,sg-id=<security-group-id>,arch=<architecture>\". "+
			formatter.Colorize("Region name is required key-value.",
				formatter.GreenColor)+
			" VPC ID, Security Group ID and Architecture are optional. "+
			"Each region needs to be added using a separate --add-region flag.")
	updateAWSProviderCmd.Flags().StringArray("add-zone", []string{},
		"[Optional] Zone associated to the AWS Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>,"+
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
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Each zone needs to be removed using a separate --remove-zone flag.")

	updateAWSProviderCmd.Flags().StringArray("edit-region", []string{},
		"[Optional] Edit region details associated with the AWS provider. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,"+
			"vpc-id=<vpc-id>,sg-id=<security-group-id>\". "+
			formatter.Colorize("Region name is a required key-value pair.",
				formatter.GreenColor)+
			" VPC ID and Security Group ID are optional. "+
			"Each region needs to be modified using a separate --edit-region flag.")
	updateAWSProviderCmd.Flags().StringArray("edit-zone", []string{},
		"[Optional] Edit zone associated to the AWS Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>,"+
			"secondary-subnet=<secondary-subnet-id>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Subnet IDs and Secondary subnet ID is optional. "+
			"Each zone needs to be modified using a separate --edit-zone flag.")

	updateAWSProviderCmd.Flags().String("ssh-user", "",
		"[Optional] Updating SSH User to access the YugabyteDB nodes.")
	updateAWSProviderCmd.Flags().Int("ssh-port", 0,
		"[Optional] Updating SSH Port to access the YugabyteDB nodes.")

	updateAWSProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads, "+
			"defaults to false.")
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
						YbImage:         util.GetStringPointer(region["yb-image"]),
						Arch:            util.GetStringPointer(region["arch"]),
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
