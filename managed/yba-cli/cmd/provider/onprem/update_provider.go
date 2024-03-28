/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

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

// updateOnpremProviderCmd represents the provider command
var updateOnpremProviderCmd = &cobra.Command{
	Use:   "update",
	Short: "Update an On-premises YugabyteDB Anywhere provider",
	Long:  "Update an On-premises provider in YugabyteDB Anywhere",
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
				"Provider: On-premises", "Update - Fetch Providers")
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
		providerCode := util.OnpremProviderType
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
		onpremCloudInfo := cloudInfo.GetOnprem()

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

		ybHomeDir, err := cmd.Flags().GetString("yb-home-dir")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(ybHomeDir) > 0 {
			logrus.Debug("Updating Yb Home Directory\n")
			onpremCloudInfo.SetYbHomeDir(ybHomeDir)
		}

		cloudInfo.SetOnprem(onpremCloudInfo)
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

		if cmd.Flags().Changed("passwordless-sudo-access") {
			logrus.Debug("Updating passwordless sudo access\n")
			passwordlessSudoAccess, err := cmd.Flags().GetBool("passwordless-sudo-access")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			details.SetPasswordlessSudoAccess(passwordlessSudoAccess)
		}

		if cmd.Flags().Changed("skip-provisioning") {
			logrus.Debug("Updating skip provisioning\n")
			skipProvisioning, err := cmd.Flags().GetBool("skip-provisioning")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			details.SetSkipProvisioning(skipProvisioning)
		}

		if cmd.Flags().Changed("install-node-exporter") {
			logrus.Debug("Updating install node exporter\n")
			installNodeExporter, err := cmd.Flags().GetBool("install-node-exporter")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			details.SetInstallNodeExporter(installNodeExporter)
		}

		nodeExporterUser, err := cmd.Flags().GetString("node-exporter-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(nodeExporterUser) > 0 {
			logrus.Debug("Updating Node exporter user\n")
			details.SetNodeExporterUser(nodeExporterUser)
		}

		if cmd.Flags().Changed("node-exporter-port") {
			nodeExporterPort, err := cmd.Flags().GetInt("node-exporter-port")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if details.GetNodeExporterPort() != int32(nodeExporterPort) {
				logrus.Debug("Updating Node exporter port\n")
				details.SetNodeExporterPort(int32(nodeExporterPort))
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

		removeRegions, err := cmd.Flags().GetStringArray("remove-region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		removeZones, err := cmd.Flags().GetStringArray("remove-zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		providerRegions = removeOnpremRegions(removeRegions, providerRegions)

		providerRegions = editOnpremRegions(editRegions, addZones, removeZones, providerRegions)

		providerRegions = addOnpremRegions(addRegions, addZones, providerRegions)

		provider.SetRegions(providerRegions)

		// End of Updating Regions

		rUpdate, response, err := authAPI.EditProvider(provider.GetUuid()).
			EditProviderRequest(provider).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Provider: On-premises", "Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rUpdate.GetResourceUUID()
		taskUUID := rUpdate.GetTaskUUID()

		providerutil.WaitForUpdateProviderTask(
			authAPI, providerName, providerUUID, providerCode, taskUUID)
	},
}

func init() {
	updateOnpremProviderCmd.Flags().SortFlags = false

	updateOnpremProviderCmd.Flags().String("new-name", "",
		"[Optional] Updating provider name.")

	updateOnpremProviderCmd.Flags().StringArray("add-region", []string{},
		"[Optional] Add region associated with the On-premises provider. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,latitude=<latitude>,longitude=<longitude>\". "+
			formatter.Colorize("Region name is a required key-value.",
				formatter.GreenColor)+
			" Latitude and Longitude are optional. "+
			"Each region needs to be added using a separate --add-region flag.")
	updateOnpremProviderCmd.Flags().StringArray("add-zone", []string{},
		"[Optional] Zone associated to the On-premises Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>\". "+
			formatter.Colorize("Zone name and Region name are required values. ",
				formatter.GreenColor)+
			"Each --add-region definition "+
			"must have atleast one corresponding --add-zone definition. Multiple"+
			" --add-zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --add-zone flag.")

	updateOnpremProviderCmd.Flags().StringArray("remove-region", []string{},
		"[Optional] Region name to be removed from the provider. "+
			"Each region to be removed needs to be provided using a separate "+
			"--remove-region definition. Removing a region removes the corresponding zones.")
	updateOnpremProviderCmd.Flags().StringArray("remove-zone", []string{},
		"[Optional] Remove zone associated to the On-premises Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>\". "+
			formatter.Colorize("Zone name, Region name are required values. ",
				formatter.GreenColor)+
			"Each zone needs to be removed using a separate --remove-zone flag.")

	updateOnpremProviderCmd.Flags().StringArray("edit-region", []string{},
		"[Optional] Edit region details associated with the On-premises provider. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,latitude=<latitude>,longitude=<longitude>\". "+
			formatter.Colorize("Region name is a required key-value.",
				formatter.GreenColor)+
			" Latitude and Longitude are optional. "+
			"Each region needs to be modified using a separate --edit-region flag.")

	updateOnpremProviderCmd.Flags().String("ssh-user", "",
		"[Optional] Updating SSH User to access the YugabyteDB nodes.")
	updateOnpremProviderCmd.Flags().Int("ssh-port", 0,
		"[Optional] Updating SSH Port to access the YugabyteDB nodes.")

	updateOnpremProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads, "+
			"defaults to false.")
	updateOnpremProviderCmd.Flags().Bool("passwordless-sudo-access", false,
		"[Optional] Can sudo actions be carried out by user without a password.")

	updateOnpremProviderCmd.Flags().Bool("skip-provisioning", false,
		"[Optional] Set to true if YugabyteDB nodes have been prepared"+
			" manually, set to false to provision during universe creation.")

	updateOnpremProviderCmd.Flags().Bool("install-node-exporter", false,
		"[Optional] Install Node exporter.")
	updateOnpremProviderCmd.Flags().String("node-exporter-user", "",
		"[Optional] Node Exporter User.")
	updateOnpremProviderCmd.Flags().Int("node-exporter-port", 0,
		"[Optional] Node Exporter Port.")
	updateOnpremProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")
	updateOnpremProviderCmd.Flags().String("yb-home-dir", "",
		"[Optional] YB Home directory.")

}

func removeOnpremRegions(
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

func editOnpremRegions(
	editRegions, addZones, removeZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {

	for i, r := range providerRegions {
		regionName := r.GetCode()
		zones := r.GetZones()
		zones = removeOnpremZones(regionName, removeZones, zones)
		zones = addOnpremZones(regionName, addZones, zones)
		r.SetZones(zones)
		if len(editRegions) != 0 {
			for _, regionString := range editRegions {
				region := providerutil.BuildRegionMapFromString(regionString, "edit")

				if strings.Compare(region["name"], regionName) == 0 {

					if len(region["latitude"]) != 0 {
						latitude, err := strconv.ParseFloat(region["latitude"], 64)
						if err != nil {
							errMessage := err.Error() + " Using latitude as 0.0\n"
							logrus.Errorln(
								formatter.Colorize(errMessage, formatter.YellowColor),
							)
							latitude = 0.0
						}
						r.SetLatitude(latitude)
					}
					if len(region["longitude"]) != 0 {
						longitude, err := strconv.ParseFloat(region["longitude"], 64)
						if err != nil {
							errMessage := err.Error() + " Using longitude as 0.0\n"
							logrus.Errorln(
								formatter.Colorize(errMessage, formatter.YellowColor),
							)
							longitude = 0.0
						}
						r.SetLongitude(longitude)
					}

				}

			}
		}
		providerRegions[i] = r
	}
	return providerRegions
}

func addOnpremRegions(
	addRegions, addZones []string,
	providerRegions []ybaclient.Region,
) []ybaclient.Region {
	if len(addRegions) == 0 {
		return providerRegions
	}
	for _, regionString := range addRegions {
		region := providerutil.BuildRegionMapFromString(regionString, "add")

		zones := addOnpremZones(region["name"], addZones, make([]ybaclient.AvailabilityZone, 0))

		latitude, err := strconv.ParseFloat(region["latitude"], 64)
		if err != nil {
			errMessage := err.Error() + " Using latitude as 0.0\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			latitude = 0.0
		}
		longitude, err := strconv.ParseFloat(region["longitude"], 64)
		if err != nil {
			errMessage := err.Error() + " Using longitude as 0.0\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			longitude = 0.0
		}
		r := ybaclient.Region{
			Code:      util.GetStringPointer(region["name"]),
			Name:      util.GetStringPointer(region["name"]),
			Latitude:  util.GetFloat64Pointer(latitude),
			Longitude: util.GetFloat64Pointer(longitude),
			Zones:     zones,
		}

		providerRegions = append(providerRegions, r)
	}

	return providerRegions
}

func removeOnpremZones(
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

func addOnpremZones(
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

		if strings.Compare(zone["region-name"], regionName) == 0 {
			z := ybaclient.AvailabilityZone{
				Code: util.GetStringPointer(zone["name"]),
				Name: zone["name"],
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
