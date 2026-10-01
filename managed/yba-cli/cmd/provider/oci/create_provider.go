/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

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

// createOCIProviderCmd represents the provider command
var createOCIProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create an OCI YugabyteDB Anywhere provider",
	Long:    "Create an OCI provider in YugabyteDB Anywhere",
	Example: `yba provider oci create -n <provider-name> \
	--compartment-id <compartment-ocid> \
	--oci-region us-ashburn-1 \
	--tenancy-id <tenancy-ocid> \
	--user-id <user-ocid> \
	--fingerprint <fingerprint> \
	--private-key-file-path <path-to-api-key.pem> \
	--region region-name=us-ashburn-1::vnet=<vcn-ocid> \
	--zone zone-name=AD-1::region-name=us-ashburn-1::subnet=<subnet-ocid>`,
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

		providerCode := util.OCIProviderType
		ociCloudInfo := ociCloudInfoFromFlags(cmd)

		airgapInstall, err := cmd.Flags().GetBool("airgap-install")
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

		regions, err := cmd.Flags().GetStringArray("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		zones, err := cmd.Flags().GetStringArray("zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		imageBundles, err := cmd.Flags().GetStringArray("image-bundle")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.Provider{
			Code:         util.GetStringPointer(providerCode),
			ImageBundles: buildOCIImageBundles(imageBundles),
			Name:         util.GetStringPointer(providerName),
			Regions:      buildOCIRegions(regions, zones),
			Details: &ybaclient.ProviderDetails{
				AirGapInstall: util.GetBoolPointer(airgapInstall),
				NtpServers:    ntpServers,
				CloudInfo: &ybaclient.CloudInfo{
					Oci: &ociCloudInfo,
				},
			},
		}
		requestBody.AllAccessKeys = make([]ybaclient.AccessKey, 0)

		if !util.IsEmptyString(keyPairName) && !util.IsEmptyString(sshFileContent) {
			accessKey := ybaclient.AccessKey{
				KeyInfo: ybaclient.KeyInfo{
					KeyPairName:          util.GetStringPointer(keyPairName),
					SshPrivateKeyContent: util.GetStringPointer(sshFileContent),
				},
			}
			requestBody.AllAccessKeys = append(requestBody.AllAccessKeys, accessKey)
		}

		rTask, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Provider: OCI", "Create")
		}

		providerutil.WaitForCreateProviderTask(
			authAPI, providerName, rTask, providerCode)
	},
}

func init() {
	createOCIProviderCmd.Flags().SortFlags = false

	createOCIProviderCmd.Flags().String("auth-type", "",
		fmt.Sprintf("[Optional] OCI authentication type. Allowed values: %s, %s. "+
			"Can also be set using environment variable %s. Defaults to %s.",
			util.OCIAuthTypeAPIKey, util.OCIAuthTypeInstancePrincipal,
			util.OCIAuthTypeEnv, util.OCIAuthTypeAPIKey))
	createOCIProviderCmd.Flags().String("tenancy-id", "",
		fmt.Sprintf("OCI Tenancy OCID. Required for %s authentication. "+
			"Can also be set using environment variable %s.",
			util.OCIAuthTypeAPIKey, util.OCITenancyIDEnv))
	createOCIProviderCmd.Flags().String("user-id", "",
		fmt.Sprintf("OCI User OCID. Required for %s authentication. "+
			"Can also be set using environment variable %s.",
			util.OCIAuthTypeAPIKey, util.OCIUserIDEnv))
	createOCIProviderCmd.Flags().String("fingerprint", "",
		fmt.Sprintf("OCI API key fingerprint. Required for %s authentication. "+
			"Can also be set using environment variable %s.",
			util.OCIAuthTypeAPIKey, util.OCIFingerprintEnv))
	createOCIProviderCmd.Flags().String("private-key-file-path", "",
		fmt.Sprintf("Path to the OCI API private key PEM file. Required for %s authentication. "+
			"Can also be set using environment variable %s.",
			util.OCIAuthTypeAPIKey, util.OCIPrivateKeyFileEnv))
	createOCIProviderCmd.MarkFlagsRequiredTogether(
		"tenancy-id", "user-id", "fingerprint", "private-key-file-path")

	createOCIProviderCmd.Flags().String("compartment-id", "",
		fmt.Sprintf("[Required] OCI Compartment OCID. "+
			"Can also be set using environment variable %s.", util.OCICompartmentIDEnv))
	createOCIProviderCmd.Flags().String("oci-region", "",
		fmt.Sprintf("[Required] Default OCI region (for example, us-ashburn-1). "+
			"Can also be set using environment variable %s.", util.OCIRegionEnv))

	createOCIProviderCmd.Flags().String("hosted-zone-id", "",
		fmt.Sprintf("[Optional] OCI DNS Zone OCID. "+
			"Can also be set using environment variable %s.", util.OCIHostedZoneIDEnv))

	createOCIProviderCmd.Flags().StringArray("region", []string{},
		"[Required] Region associated with the OCI provider. Minimum number of required "+
			"regions = 1. Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"region-name=<region-name>::vnet=<vcn-ocid>::instance-template=<instance-config-ocid>\". "+
			formatter.Colorize("Region name and Virtual network are required key-values.",
				formatter.GreenColor)+
			" Instance template is the OCID of an OCI instance configuration and is optional. "+
			"Each region needs to be added using a separate --region flag. "+
			"Example: --region region-name=us-ashburn-1::vnet=<vcn-ocid>")
	createOCIProviderCmd.Flags().StringArray("zone", []string{},
		"[Required] Zone associated to the OCI Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-ocid>\"."+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Each --region definition "+
			"must have atleast one corresponding --zone definition. Multiple --zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --zone flag. "+
			"Example: --zone zone-name=AD-1::region-name=us-ashburn-1::subnet=<subnet-ocid>")

	createOCIProviderCmd.Flags().StringArray("image-bundle", []string{},
		"[Optional] Image bundles associated with OCI provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>::arch=<architecture>::"+
			"machine-image=<image-ocid>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle name, machine image and SSH user are required key-value pairs.",
				formatter.GreenColor)+
			fmt.Sprintf(
				" Allowed values for architecture are %s and %s, defaults to %s.",
				util.X86_64, util.AARCH64, util.X86_64)+
			" The default SSH user is opc and the default SSH Port is 22. "+
			"Default marks the image bundle as default for the provider. "+
			"Each image bundle can be added using separate --image-bundle flag. "+
			"Example: --image-bundle image-bundle-name=<image-bundle>::machine-image=<image-ocid>::"+
			"ssh-user=opc::ssh-port=22")

	createOCIProviderCmd.Flags().String("custom-ssh-keypair-name", "",
		"[Optional] Provide custom key pair name to access YugabyteDB nodes. "+
			"If left empty, "+
			"YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.")
	createOCIProviderCmd.Flags().String("custom-ssh-keypair-file-path", "",
		"[Optional] Provide custom key pair file path to access YugabyteDB nodes. "+
			formatter.Colorize("Required with --custom-ssh-keypair-name.",
				formatter.GreenColor))
	createOCIProviderCmd.MarkFlagsRequiredTogether("custom-ssh-keypair-name",
		"custom-ssh-keypair-file-path")

	createOCIProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads. "+
			"(default false)")
	createOCIProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")
}

func ociCloudInfoFromFlags(cmd *cobra.Command) ybaclient.OCICloudInfo {
	var ociCloudInfo ybaclient.OCICloudInfo

	authType, err := cmd.Flags().GetString("auth-type")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(authType) == 0 {
		authType = util.OCIAuthTypeFromEnv()
	}
	authType, err = util.NormalizeOCIAuthType(authType)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	ociCloudInfo.SetOciAuthType(authType)

	ociCloudInfo.SetOciCompartmentId(
		requiredOCIValueFromFlagOrEnv(cmd, "compartment-id", util.OCICompartmentIDEnv))
	ociCloudInfo.SetOciRegion(
		requiredOCIValueFromFlagOrEnv(cmd, "oci-region", util.OCIRegionEnv))

	tenancyID, userID, fingerprint, keyPath := ociAPIKeyFlags(cmd)
	apiKeyFlagsSet := len(tenancyID) > 0 || len(userID) > 0 ||
		len(fingerprint) > 0 || len(keyPath) > 0
	switch authType {
	case util.OCIAuthTypeInstancePrincipal:
		if apiKeyFlagsSet {
			logrus.Fatalln(formatter.Colorize(
				fmt.Sprintf("API key flags cannot be set with %s auth-type\n",
					util.OCIAuthTypeInstancePrincipal),
				formatter.RedColor))
		}
	case util.OCIAuthTypeAPIKey:
		if apiKeyFlagsSet {
			privateKey, err := util.ReadFileToString(keyPath)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			ociCloudInfo.SetOciTenancyId(tenancyID)
			ociCloudInfo.SetOciUserId(userID)
			ociCloudInfo.SetOciFingerprint(fingerprint)
			ociCloudInfo.SetOciPrivateKeyContent(*privateKey)
		} else {
			creds, err := util.OCIAPIKeyCredentialsFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			ociCloudInfo.SetOciTenancyId(creds.TenancyID)
			ociCloudInfo.SetOciUserId(creds.UserID)
			ociCloudInfo.SetOciFingerprint(creds.Fingerprint)
			ociCloudInfo.SetOciPrivateKeyContent(creds.PrivateKeyContent)
		}
	}

	hostedZoneID, err := cmd.Flags().GetString("hosted-zone-id")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(hostedZoneID) == 0 {
		hostedZoneID, _ = util.OCIValueFromEnv(util.OCIHostedZoneIDEnv)
	}
	if len(hostedZoneID) > 0 {
		ociCloudInfo.SetOciHostedZoneId(hostedZoneID)
	}
	return ociCloudInfo
}

// requiredOCIValueFromFlagOrEnv returns the flag value, or its environment variable
// value when the flag is empty, and fails if neither is set
func requiredOCIValueFromFlagOrEnv(cmd *cobra.Command, flagName, envName string) string {
	value, err := cmd.Flags().GetString(flagName)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(value) > 0 {
		return value
	}
	value, err = util.OCIValueFromEnv(envName)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(
			fmt.Sprintf("Missing required value: --%s (or %s)\n", flagName, envName),
			formatter.RedColor))
	}
	return value
}

func ociAPIKeyFlags(cmd *cobra.Command) (tenancyID, userID, fingerprint, keyPath string) {
	var err error
	tenancyID, err = cmd.Flags().GetString("tenancy-id")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	userID, err = cmd.Flags().GetString("user-id")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	fingerprint, err = cmd.Flags().GetString("fingerprint")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	keyPath, err = cmd.Flags().GetString("private-key-file-path")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	return tenancyID, userID, fingerprint, keyPath
}

func buildOCIRegions(regionStrings, zoneStrings []string) (res []ybaclient.Region) {
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

		zones := buildOCIZones(zoneStrings, region["name"])
		r := ybaclient.Region{
			Code: util.GetStringPointer(region["name"]),
			Name: util.GetStringPointer(region["name"]),
			Details: &ybaclient.RegionDetails{
				CloudInfo: &ybaclient.RegionCloudInfo{
					Oci: &ybaclient.OCIRegionCloudInfo{
						Vnet:             util.GetStringPointer(region["vnet"]),
						InstanceTemplate: util.GetStringPointer(region["instance-template"]),
					},
				},
			},
			Zones: zones,
		}
		res = append(res, r)
	}
	return res
}

func buildOCIZones(zoneStrings []string, regionName string) (res []ybaclient.AvailabilityZone) {
	for _, zoneString := range zoneStrings {
		zone := providerutil.BuildZoneMapFromString(zoneString, "")

		if _, ok := zone["subnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Subnet not specified in zone info.\n",
					formatter.RedColor))
		}

		if strings.Compare(zone["region-name"], regionName) == 0 {
			z := ybaclient.AvailabilityZone{
				Code:   util.GetStringPointer(zone["name"]),
				Name:   zone["name"],
				Subnet: util.GetStringPointer(zone["subnet"]),
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

func buildOCIImageBundles(imageBundles []string) []ybaclient.ImageBundle {
	imageBundleLen := len(imageBundles)
	res := make([]ybaclient.ImageBundle, 0)
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
		if imageBundleLen == 1 && !defaultBundle {
			defaultBundle = true
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
		res = append(res, imageBundle)
	}
	return res
}
