/*
 * Copyright (c) YugabyteDB, Inc.
 */

package aws

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

// createAWSProviderCmd represents the provider command
var createAWSProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create an AWS YugabyteDB Anywhere provider",
	Long:    "Create an AWS provider in YugabyteDB Anywhere",
	Example: `yba provider aws create -n <provider-name> \
	--region region-name=us-west-2::vpc-id=<vpc-id>::sg-id=<security-group> \
	--zone zone-name=us-west-2a::region-name=us-west-2::subnet=<subnet> \
	--zone zone-name=us-west-2b::region-name=us-west-2::subnet=<subnet> \
	--access-key-id <aws-access-key-id> --secret-access-key <aws-secret-access-key>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to create\n", formatter.RedColor))
		}
		isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
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

		allowed, version, err := authAPI.NewProviderYBAVersionCheck()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		providerCode := util.AWSProviderType

		var awsCloudInfo ybaclient.AWSCloudInfo

		isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		hostedZoneID, err := cmd.Flags().GetString("hosted-zone-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		awsCloudInfo.SetAwsHostedZoneId(hostedZoneID)

		if !isIAM {
			accessKeyID, err := cmd.Flags().GetString("access-key-id")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			secretAccessKey, err := cmd.Flags().GetString("secret-access-key")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(accessKeyID) == 0 && len(secretAccessKey) == 0 {
				awsCreds, err := util.AwsCredentialsFromEnv()
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				awsCloudInfo.SetAwsAccessKeyID(awsCreds.AccessKeyID)
				awsCloudInfo.SetAwsAccessKeySecret(awsCreds.SecretAccessKey)
			} else {
				awsCloudInfo.SetAwsAccessKeyID(accessKeyID)
				awsCloudInfo.SetAwsAccessKeySecret(secretAccessKey)
			}
		}

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

		skipKeyValidateAndUpload, err := cmd.Flags().GetBool("skip-ssh-keypair-validation")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		allAccessKeys := make([]ybaclient.AccessKey, 0)
		accessKey := ybaclient.AccessKey{
			KeyInfo: ybaclient.KeyInfo{
				KeyPairName:              util.GetStringPointer(keyPairName),
				SshPrivateKeyContent:     util.GetStringPointer(sshFileContent),
				SkipKeyValidateAndUpload: util.GetBoolPointer(skipKeyValidateAndUpload),
			},
		}
		allAccessKeys = append(allAccessKeys, accessKey)

		regions, err := cmd.Flags().GetStringArray("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		zones, err := cmd.Flags().GetStringArray("zone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		awsRegions := buildAWSRegions(regions, zones, allowed, version)

		imageBundles, err := cmd.Flags().GetStringArray("image-bundle")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		regionOverrides, err := cmd.Flags().GetStringArray("image-bundle-region-override")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		awsImageBundles := buildAWSImageBundles(imageBundles, regionOverrides, len(awsRegions))

		requestBody := ybaclient.Provider{
			Code:          util.GetStringPointer(providerCode),
			AllAccessKeys: allAccessKeys,
			ImageBundles:  awsImageBundles,
			Name:          util.GetStringPointer(providerName),
			Regions:       awsRegions,
			Details: &ybaclient.ProviderDetails{
				AirGapInstall: util.GetBoolPointer(airgapInstall),
				NtpServers:    ntpServers,
				CloudInfo: &ybaclient.CloudInfo{
					Aws: &awsCloudInfo,
				},
			},
		}

		rTask, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Provider: AWS", "Create")
		}

		providerutil.WaitForCreateProviderTask(
			authAPI, providerName, rTask, providerCode)
	},
}

func init() {
	createAWSProviderCmd.Flags().SortFlags = false

	// Flags needed for AWS
	createAWSProviderCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("AWS Access Key ID. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for non IAM role based providers.",
				formatter.GreenColor),
			util.AWSAccessKeyEnv))
	createAWSProviderCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("AWS Secret Access Key. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize("Required for non IAM role based providers.",
				formatter.GreenColor),
			util.AWSSecretAccessKeyEnv))
	createAWSProviderCmd.MarkFlagsRequiredTogether("access-key-id", "secret-access-key")
	createAWSProviderCmd.Flags().Bool("use-iam-instance-profile", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. Provider "+
			"creation will fail on insufficient permissions on the host. (default false)")
	createAWSProviderCmd.Flags().String("hosted-zone-id", "",
		"[Optional] Hosted Zone ID corresponding to Amazon Route53.")

	createAWSProviderCmd.Flags().StringArray("region", []string{},
		"[Required] Region associated with the AWS provider. Minimum number of required "+
			"regions = 1. Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"region-name=<region-name>::"+
			"vpc-id=<vpc-id>::sg-id=<security-group-id>\". "+
			formatter.Colorize("Region name is required key-value.",
				formatter.GreenColor)+
			" VPC ID and Security Group ID"+
			" are optional. "+
			"Each region needs to be added using a separate --region flag. "+
			"Example: --region region-name=us-west-2::vpc-id=<vpc-id>::sg-id=<security-group> "+
			"--region region-name=us-east-2::vpc-id=<vpc-id>::sg-id=<security-group>")
	createAWSProviderCmd.Flags().StringArray("zone", []string{},
		"[Required] Zone associated to the AWS Region defined. "+
			"Provide the following double colon (::) separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>::"+
			"secondary-subnet=<secondary-subnet-id>\". "+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Secondary subnet ID is optional. Each --region definition "+
			"must have atleast one corresponding --zone definition. Multiple --zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --zone flag. "+
			"Example: --zone zone-name=us-west-2a::region-name=us-west-2::subnet=<subnet-id>"+
			" --zone zone-name=us-west-2b::region-name=us-west-2::subnet=<subnet-id>")

	createAWSProviderCmd.Flags().StringArray("image-bundle", []string{},
		"[Optional] Image bundles associated with AWS provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>::arch=<architecture>::"+
			"ssh-user=<ssh-user>::ssh-port=<ssh-port>::imdsv2=<true/false>::default=<true/false>\". "+
			formatter.Colorize(
				"Image bundle name, architecture and SSH user are required key-value pairs.",
				formatter.GreenColor)+
			" The default for SSH Port is 22, IMDSv2 ("+
			"This should be true if the Image bundle requires Instance Metadata Service v2)"+
			" is false. Default marks the image bundle as default for the provider. "+
			"Allowed values for architecture are x86_64 and arm64/aarch64."+
			"Each image bundle can be added using separate --image-bundle flag. "+
			"Example: --image-bundle image-bundle-name=<name>::"+
			"ssh-user=<ssh-user>::ssh-port=22")
	createAWSProviderCmd.Flags().StringArray("image-bundle-region-override", []string{},
		"[Optional] Image bundle region overrides associated with AWS provider. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"image-bundle-name=<image-bundle-name>,region-name=<region-name>,"+
			"machine-image=<machine-image>\". "+
			formatter.Colorize(
				"Image bundle name and region name are required key-value pairs.",
				formatter.GreenColor)+" Each --image-bundle definition "+
			"must have atleast one corresponding --image-bundle-region-override "+
			"definition for every region added."+
			" Each image bundle override can be added using separate --image-bundle-region-override flag. "+
			"Example: --image-bundle-region-override image-bundle-name=<name>::"+
			"region-name=<region-name>::machine-image=<machine-image>")

	createAWSProviderCmd.Flags().String("custom-ssh-keypair-name", "",
		"[Optional] Provide custom key pair name to access YugabyteDB nodes. "+
			"If left empty, "+
			"YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.")
	createAWSProviderCmd.Flags().String("custom-ssh-keypair-file-path", "",
		fmt.Sprintf("[Optional] Provide custom key pair file path to access YugabyteDB nodes. %s",
			formatter.Colorize("Required with --custom-ssh-keypair-name.",
				formatter.GreenColor)))
	createAWSProviderCmd.MarkFlagsRequiredTogether("custom-ssh-keypair-name",
		"custom-ssh-keypair-file-path")
	createAWSProviderCmd.Flags().Bool("skip-ssh-keypair-validation", false,
		"[Optional] Skip ssh keypair validation and upload to AWS. (default false)")

	createAWSProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads. "+
			"(default false)")
	createAWSProviderCmd.Flags().StringArray("ntp-servers", []string{},
		"[Optional] List of NTP Servers. Can be provided as separate flags or "+
			"as comma-separated values.")

}

func buildAWSRegions(regionStrings, zoneStrings []string, allowed bool,
	version string) (res []ybaclient.Region) {
	if len(regionStrings) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one region is required per provider.\n",
				formatter.RedColor))
	}
	for _, regionString := range regionStrings {
		region := providerutil.BuildRegionMapFromString(regionString, "")

		zones := buildAWSZones(zoneStrings, region["name"])
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
		if !allowed {
			logrus.Info(
				fmt.Sprintf("YugabyteDB Anywhere version %s does not support specifying "+
					"Architecture, ignoring value.\n", version))
		}
		res = append(res, r)
	}
	return res
}

func buildAWSZones(zoneStrings []string, regionName string) (res []ybaclient.AvailabilityZone) {
	for _, zoneString := range zoneStrings {
		zone := providerutil.BuildZoneMapFromString(zoneString, "")

		if _, ok := zone["subnet"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Subnet not specified in zone info.\n",
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
			formatter.Colorize("Atleast one zone is required per region.\n",
				formatter.RedColor))
	}
	return res
}

func buildAWSImageBundles(
	imageBundles, regionOverrides []string,
	numberOfRegions int) []ybaclient.ImageBundle {
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

		regionOverrides := buildAWSImageBundleRegionOverrides(regionOverrides, bundle["name"])

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
		res = append(res, imageBundle)
	}
	return res
}

func buildAWSImageBundleRegionOverrides(
	regionOverrides []string,
	name string) map[string]ybaclient.BundleInfo {
	res := map[string]ybaclient.BundleInfo{}
	for _, r := range regionOverrides {
		override := providerutil.BuildImageBundleRegionOverrideMapFromString(r, "add")
		if strings.Compare(override["name"], name) == 0 {
			res[override["region-name"]] = ybaclient.BundleInfo{
				YbImage: util.GetStringPointer(override["machine-image"]),
			}
		}
	}
	return res
}
