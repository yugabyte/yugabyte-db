/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

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

// createOnpremProviderCmd represents the provider command
var createOnpremProviderCmd = &cobra.Command{
	Use:   "create",
	Short: "Create an On-premises YugabyteDB Anywhere provider",
	Long: "Create an On-premises provider in YugabyteDB Anywhere. " +
		"To utilize the on-premises provider in universes, manage instance types" +
		" and node instances using the \"yba provider onprem instance-types/node [operation]\" " +
		"set of commands",
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

		providerCode := util.OnpremProviderType

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

		keyPairName, err := cmd.Flags().GetString("ssh-keypair-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		filePath, err := cmd.Flags().GetString("ssh-keypair-file-path")
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

		passwordlessSudoAccess, err := cmd.Flags().GetBool("passwordless-sudo-access")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		skipProvisioning, err := cmd.Flags().GetBool("skip-provisioning")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		installNodeExporter, err := cmd.Flags().GetBool("install-node-exporter")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var nodeExporterUser string
		var nodeExporterPort int
		if installNodeExporter {
			nodeExporterUser, err = cmd.Flags().GetString("node-exporter-user")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			nodeExporterPort, err = cmd.Flags().GetInt("node-exporter-port")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		ybHomeDir, err := cmd.Flags().GetString("yb-home-dir")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		ntpServers, err := cmd.Flags().GetStringArray("ntp-servers")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		regions, err := cmd.Flags().GetStringArray("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		zones, err := cmd.Flags().GetStringArray("zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		allAccessKeys := make([]ybaclient.AccessKey, 0)
		accessKey := ybaclient.AccessKey{
			KeyInfo: ybaclient.KeyInfo{
				KeyPairName:          util.GetStringPointer(keyPairName),
				SshPrivateKeyContent: util.GetStringPointer(sshFileContent),
			},
		}
		allAccessKeys = append(allAccessKeys, accessKey)

		requestBody := ybaclient.Provider{
			Code:          util.GetStringPointer(providerCode),
			Name:          util.GetStringPointer(providerName),
			AllAccessKeys: &allAccessKeys,
			Regions:       buildOnpremRegions(regions, zones),
			Details: &ybaclient.ProviderDetails{
				AirGapInstall: util.GetBoolPointer(airgapInstall),
				CloudInfo: &ybaclient.CloudInfo{
					Onprem: &ybaclient.OnPremCloudInfo{
						YbHomeDir: util.GetStringPointer(ybHomeDir),
					},
				},
				InstallNodeExporter:    util.GetBoolPointer(installNodeExporter),
				NodeExporterPort:       util.GetInt32Pointer(int32(nodeExporterPort)),
				NodeExporterUser:       util.GetStringPointer(nodeExporterUser),
				NtpServers:             util.StringSliceFromString(ntpServers),
				PasswordlessSudoAccess: util.GetBoolPointer(passwordlessSudoAccess),
				SkipProvisioning:       util.GetBoolPointer(skipProvisioning),
				SshPort:                util.GetInt32Pointer(int32(sshPort)),
				SshUser:                util.GetStringPointer(sshUser),
			},
		}
		rCreate, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Provider: On-premises", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rCreate.GetResourceUUID()
		taskUUID := rCreate.GetTaskUUID()

		providerutil.WaitForCreateProviderTask(authAPI,
			providerName, providerUUID, providerCode, taskUUID)
	},
}

func init() {
	createOnpremProviderCmd.Flags().SortFlags = false

	createOnpremProviderCmd.Flags().String("ssh-keypair-name", "",
		"[Required] Provider key pair name to access YugabyteDB nodes.")
	createOnpremProviderCmd.Flags().String("ssh-keypair-file-path", "",
		fmt.Sprintf("[Optional] Provider key pair file path to access YugabyteDB nodes. %s",
			formatter.Colorize("One of ssh-keypair-file-path or ssh-keypair-file-contents"+
				"required with --ssh-keypair-name.",
				formatter.GreenColor)))
	createOnpremProviderCmd.Flags().String("ssh-keypair-file-contents", "",
		fmt.Sprintf("[Optional] Provider key pair file contents to access YugabyteDB nodes. %s",
			formatter.Colorize("One of ssh-keypair-file-path or ssh-keypair-file-contents"+
				"required with --ssh-keypair-name.",
				formatter.GreenColor)))
	createOnpremProviderCmd.Flags().String("ssh-user", "", "[Required] SSH User.")
	createOnpremProviderCmd.MarkFlagRequired("ssh-user")
	createOnpremProviderCmd.MarkFlagRequired("ssh-keypair-name")
	createOnpremProviderCmd.MarkFlagRequired("ssh-keypair-file-path")
	createOnpremProviderCmd.Flags().Int("ssh-port", 22,
		"[Optional] SSH Port.")

	createOnpremProviderCmd.Flags().StringArray("region", []string{},
		"[Required] Region associated with the On-premises provider. Minimum number of required "+
			"regions = 1. Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,latitude=<latitude>,longitude=<longitude>\". "+
			formatter.Colorize("Region name is a required key-value.",
				formatter.GreenColor)+
			" Latitude and Longitude (Defaults to 0.0) are optional. "+
			"Each region needs to be added using a separate --region flag. "+
			"Example: --region region-name=us-west-1 --region region-name=us-west-2")
	createOnpremProviderCmd.Flags().StringArray("zone", []string{},
		"[Required] Zone associated to the On-premises Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>\". "+
			formatter.Colorize("Zone name and Region name are required values. ",
				formatter.GreenColor)+
			"Each --region definition must have atleast one corresponding --zone definition."+
			" Multiple --zone definitions can be provided per region."+
			" Each zone needs to be added using a separate --zone flag. "+
			"Example: --zone zone-name=us-west-1a,region-name=us-west-1"+
			" --zone zone-name=us-west-1b,region-name=us-west-1")

	createOnpremProviderCmd.Flags().Bool("passwordless-sudo-access", true,
		"[Optional] Can sudo actions be carried out by user without a password.")
	createOnpremProviderCmd.Flags().Bool("skip-provisioning", false,
		"[Optional] Set to true if YugabyteDB nodes have been prepared"+
			" manually, set to false to provision during universe creation, defaults to false.")

	createOnpremProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads, "+
			"defaults to false.")
	createOnpremProviderCmd.Flags().Bool("install-node-exporter", true,
		"[Optional] Install Node exporter.")
	createOnpremProviderCmd.Flags().String("node-exporter-user", "prometheus",
		"[Optional] Node Exporter User.")
	createOnpremProviderCmd.Flags().Int("node-exporter-port", 9300,
		"[Optional] Node Exporter Port.")
	createOnpremProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")
	createOnpremProviderCmd.Flags().String("yb-home-dir", "",
		"[Optional] YB Home directory.")

}

func buildOnpremRegions(regionStrings, zoneStrings []string) (
	res []ybaclient.Region) {
	if len(regionStrings) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one region is required per provider.\n",
				formatter.RedColor))
	}
	for _, regionString := range regionStrings {
		region := providerutil.BuildRegionMapFromString(regionString, "")
		if _, ok := region["latitude"]; !ok {
			region["latitude"] = "0.0"

		}
		if _, ok := region["longitude"]; !ok {
			region["longitude"] = "0.0"
		}

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

		zones := buildOnpremZones(zoneStrings, region["name"])
		r := ybaclient.Region{
			Code:      util.GetStringPointer(region["name"]),
			Name:      util.GetStringPointer(region["name"]),
			Latitude:  util.GetFloat64Pointer(latitude),
			Longitude: util.GetFloat64Pointer(longitude),
			Zones:     zones,
		}
		res = append(res, r)
	}
	return res
}

func buildOnpremZones(zoneStrings []string, regionName string) (res []ybaclient.AvailabilityZone) {
	for _, zoneString := range zoneStrings {
		zone := providerutil.BuildZoneMapFromString(zoneString, "")

		if strings.Compare(zone["region-name"], regionName) == 0 {
			z := ybaclient.AvailabilityZone{
				Code: util.GetStringPointer(zone["name"]),
				Name: zone["name"],
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
