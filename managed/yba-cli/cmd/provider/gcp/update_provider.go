/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

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

// updateGCPProviderCmd represents the provider command
var updateGCPProviderCmd = &cobra.Command{
	Use:   "update",
	Short: "Update a GCP YugabyteDB Anywhere provider",
	Long:  "Update a GCP provider in YugabyteDB Anywhere",
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
		if useHostCredsFlagSet := cmd.Flags().Changed("use-host-credentials"); useHostCredsFlagSet {
			isIAM, err = cmd.Flags().GetBool("use-host-credentials")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		gcpCredsFilePath, err := cmd.Flags().GetString("credentials")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if isIAM && len(gcpCredsFilePath) > 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Cannot set both credentials and use-host-credentials"+
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
				"Provider: GCP", "Update - Fetch Providers")
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
		providerCode := util.GCPProviderType
		for _, p := range r {
			if p.GetCode() == providerCode {
				provider = p
			}
		}

		if len(strings.TrimSpace(provider.GetName())) == 0 {
			errMessage := fmt.Sprintf(
				"No provider %s in cloud type %s.\n",
				providerName,
				providerCode)
			logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
		}

		providerRegions := provider.GetRegions()
		details := provider.GetDetails()
		cloudInfo := details.GetCloudInfo()
		gcpCloudInfo := cloudInfo.GetGcp()

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

		network, err := cmd.Flags().GetString("network")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(network) > 0 {
			logrus.Debug("Updating Network\n")
			gcpCloudInfo.SetDestVpcId(network)
		}

		ybFirewallTags, err := cmd.Flags().GetString("yb-firewall-tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(ybFirewallTags) > 0 {
			logrus.Debug("Updating GCP Firewall Tags\n")
			gcpCloudInfo.SetYbFirewallTags(ybFirewallTags)
		}

		var createVPC bool
		if cmd.Flags().Changed("create-vpc") {
			logrus.Debug("Updating Create VPC\n")
			createVPC, err = cmd.Flags().GetBool("create-vpc")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if createVPC {
				gcpCloudInfo.SetUseHostVPC(false)
				if len(network) > 0 {
					logrus.Debug("Updating Network\n")
					gcpCloudInfo.SetDestVpcId(network)
				} else {
					errMessage := "Network required if create-vpc is set\n"
					logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
				}
			}
		}

		if !createVPC {
			if cmd.Flags().Changed("use-host-vpc") {
				logrus.Debug("Updating use host VPC\n")
				useHostVPC, err := cmd.Flags().GetBool("use-host-vpc")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				if useHostVPC {
					gcpCloudInfo.SetUseHostVPC(useHostVPC)
					gcpCloudInfo.SetDestVpcId("")
				} else {
					gcpCloudInfo.SetUseHostVPC(true)
					if len(network) > 0 {
						logrus.Debug("Updating Network\n")
						gcpCloudInfo.SetDestVpcId(network)
					} else {
						errMessage := "Network required if use-host-vpc is not set\n"
						logrus.Fatalf(formatter.Colorize(errMessage, formatter.RedColor))
					}
				}
			}
		}

		projectID, err := cmd.Flags().GetString("project-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(projectID) > 0 {
			logrus.Debug("Updating Project ID\n")
			gcpCloudInfo.SetGceProject(projectID)
		}

		sharedVPCProjectID, err := cmd.Flags().GetString("shared-vpc-project-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(sharedVPCProjectID) > 0 {
			logrus.Debug("Updating Shared VPC Project ID\n")
			gcpCloudInfo.SetSharedVPCProject(sharedVPCProjectID)
		}

		var isIAM bool
		if useHostCredsFlagSet := cmd.Flags().Changed("use-host-credentials"); useHostCredsFlagSet {
			isIAM, err = cmd.Flags().GetBool("use-host-credentials")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debug("Updating provider to use IAM instance profile\n")
			gcpCloudInfo.SetUseHostCredentials(isIAM)
			if isIAM {
				gcpCloudInfo.SetGceApplicationCredentials("")
			}
		}

		if !gcpCloudInfo.GetUseHostCredentials() {
			credentialsFilePath, err := cmd.Flags().GetString("credentials")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			if len(credentialsFilePath) != 0 {
				gcpCreds, err := util.GcpGetCredentialsAsStringFromFilePath(credentialsFilePath)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				logrus.Debug("Updating GCP credentials\n")
				gcpCloudInfo.SetGceApplicationCredentials(gcpCreds)
				gcpCloudInfo.SetUseHostCredentials(false)
			} else if gcpCloudInfo.GetGceApplicationCredentials() == "" {
				logrus.Fatalf(formatter.Colorize("No credentials found for provider"+"\n", formatter.RedColor))
			}
		}
		cloudInfo.SetGcp(gcpCloudInfo)
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

		editRegions, err := cmd.Flags().GetStringArray("edit-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		removeRegions, err := cmd.Flags().GetStringArray("remove-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerRegions = removeGCPRegions(removeRegions, providerRegions)

		providerRegions = editGCPRegions(editRegions, providerRegions)

		providerRegions = addGCPRegions(addRegions, providerRegions)

		provider.SetRegions(providerRegions)

		// End of Updating Regions

		rUpdate, response, err := authAPI.EditProvider(provider.GetUuid()).
			EditProviderRequest(provider).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider: GCP", "Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rUpdate.GetResourceUUID()
		taskUUID := rUpdate.GetTaskUUID()

		providerutil.WaitForUpdateProviderTask(
			authAPI, providerName, providerUUID, providerCode, taskUUID)
	},
}

func init() {
	updateGCPProviderCmd.Flags().SortFlags = false

	updateGCPProviderCmd.Flags().String("new-name", "",
		"[Optional] Updating provider name.")

	updateGCPProviderCmd.Flags().Bool("use-host-credentials", false,
		"[Optional] Enabling YugabyteDB Anywhere Host credentials in GCP. "+
			"Explicitly mark as false to disable on a provider made with host credentials.")
	updateGCPProviderCmd.Flags().String("credentials", "",
		fmt.Sprintf("[Optional] GCP Service Account credentials file path. %s",
			formatter.Colorize("Required for providers not using host credentials.",
				formatter.GreenColor)))

	updateGCPProviderCmd.Flags().String("network", "",
		fmt.Sprintf("[Optional] Update Custom GCE network name. %s",
			formatter.Colorize("Required if create-vpc is true or use-host-vpc is false.",
				formatter.GreenColor)))
	updateGCPProviderCmd.Flags().String("yb-firewall-tags", "",
		"[Optional] Update tags for firewall rules in GCP.")
	updateGCPProviderCmd.Flags().Bool("create-vpc", false,
		"[Optional] Creating a new VPC network in GCP (Beta Feature). "+
			"Specify VPC name using --network.")
	updateGCPProviderCmd.Flags().Bool("use-host-vpc", false,
		"[Optional] Using VPC from YugabyteDB Anywhere Host. "+
			"If set to false, specify an exsiting VPC using --network. "+
			"Ignored if create-vpc is set.")
	updateGCPProviderCmd.Flags().String("project-id", "",
		"[Optional] Update project ID that hosts universe nodes in GCP.")
	updateGCPProviderCmd.Flags().String("shared-vpc-project-id", "",
		"[Optional] Update shared VPC project ID in GCP.")

	updateGCPProviderCmd.Flags().StringArray("add-region", []string{},
		"[Required] Region associated with the GCP provider. Minimum number of required "+
			"regions = 1. Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,shared-subnet=<subnet-id>,yb-image=<custom-ami>,"+
			"instance-template=<instance-templates-for-YugabyteDB-nodes>\". "+
			formatter.Colorize("Region name and Shared subnet are required key-value pairs.",
				formatter.GreenColor)+
			" YB Image (AMI) and Instance Template are optional. "+
			"Each region can be added using separate --add-region flags.")
	updateGCPProviderCmd.Flags().StringArray("edit-region", []string{},
		"[Optional] Edit region details associated with the GCP provider. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,shared-subnet=<subnet-id>,yb-image=<custom-ami>,"+
			"instance-template=<instance-templates-for-YugabyteDB-nodes>\". "+
			formatter.Colorize("Region name is a required key-value pair.",
				formatter.GreenColor)+
			" Shared subnet, YB Image (AMI) and Instance Template are optional. "+
			"Each region needs to be modified using a separate --edit-region flag.")
	updateGCPProviderCmd.Flags().StringArray("remove-region", []string{},
		"[Optional] Region name to be removed from the provider. "+
			"Each region to be removed needs to be provided using a separate "+
			"--remove-region definition. Removing a region removes the corresponding zones.")

	updateGCPProviderCmd.Flags().String("ssh-user", "",
		"[Optional] Updating SSH User to access the YugabyteDB nodes.")
	updateGCPProviderCmd.Flags().Int("ssh-port", 0,
		"[Optional] Updating SSH Port to access the YugabyteDB nodes.")

	updateGCPProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads.")
	updateGCPProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")

}

func removeGCPRegions(
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

func editGCPRegions(
	editRegions []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {

	for i, r := range providerRegions {
		regionName := r.GetCode()
		if len(editRegions) != 0 {
			for _, regionString := range editRegions {
				region := providerutil.BuildRegionMapFromString(regionString, "edit")

				if strings.Compare(region["name"], regionName) == 0 {
					details := r.GetDetails()
					cloudInfo := details.GetCloudInfo()
					gcp := cloudInfo.GetGcp()
					if len(region["yb-image"]) != 0 {
						gcp.SetYbImage(region["yb-image"])
					}
					if len(region["shared-subnet"]) != 0 {
						zones := r.GetZones()
						for i, z := range zones {
							z.SetSubnet(region["shared-subnet"])
							zones[i] = z
						}
						r.SetZones(zones)
					}
					if len(region["instance-template"]) != 0 {
						gcp.SetInstanceTemplate(region["instance-template"])
					}
					cloudInfo.SetGcp(gcp)
					details.SetCloudInfo(cloudInfo)
					r.SetDetails(details)
				}

			}
		}
		providerRegions[i] = r
	}
	return providerRegions
}

func addGCPRegions(
	addRegions []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {
	if len(addRegions) == 0 {
		return providerRegions
	}
	for _, regionString := range addRegions {
		region := providerutil.BuildRegionMapFromString(regionString, "add")

		zones := addGCPZones(region["name"], region["shared-subnet"],
			make([]ybaclient.AvailabilityZone, 0))
		r := ybaclient.Region{
			Code:  util.GetStringPointer(region["name"]),
			Name:  util.GetStringPointer(region["name"]),
			Zones: zones,
			Details: &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Gcp: &ybaclient.GCPRegionCloudInfo{
						YbImage:          util.GetStringPointer(region["yb-image"]),
						InstanceTemplate: util.GetStringPointer(region["instance-template"]),
					},
				},
			},
		}
		providerRegions = append(providerRegions, r)
	}

	return providerRegions
}

func addGCPZones(
	regionName, sharedSubnet string,
	zones []ybaclient.AvailabilityZone,
) []ybaclient.AvailabilityZone {
	z := ybaclient.AvailabilityZone{
		Subnet: util.GetStringPointer(sharedSubnet),
	}
	zones = append(zones, z)
	if len(zones) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}
	return zones
}
