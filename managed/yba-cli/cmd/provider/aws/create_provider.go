/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"fmt"
	"os"
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
	Use:   "create",
	Short: "Create an AWS YugabyteDB Anywhere provider",
	Long:  "Create an AWS provider in YugabyteDB Anywhere",
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

		sshUser, err := cmd.Flags().GetString("ssh-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sshPort, err := cmd.Flags().GetInt("ssh-port")
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

		allAccessKeys := make([]ybaclient.AccessKey, 0)
		accessKey := ybaclient.AccessKey{
			KeyInfo: ybaclient.KeyInfo{
				KeyPairName:          util.GetStringPointer(keyPairName),
				SshPrivateKeyContent: util.GetStringPointer(sshFileContent),
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

		requestBody := ybaclient.Provider{
			Code:          util.GetStringPointer(providerCode),
			AllAccessKeys: &allAccessKeys,
			Name:          util.GetStringPointer(providerName),
			Regions:       buildAWSRegions(regions, zones, allowed, version),
			Details: &ybaclient.ProviderDetails{
				AirGapInstall: util.GetBoolPointer(airgapInstall),
				SshPort:       util.GetInt32Pointer(int32(sshPort)),
				SshUser:       util.GetStringPointer(sshUser),
				NtpServers:    util.StringSliceFromString(ntpServers),
				CloudInfo: &ybaclient.CloudInfo{
					Aws: &awsCloudInfo,
				},
			},
		}

		rCreate, response, err := authAPI.CreateProvider().
			CreateProviderRequest(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider: AWS", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		providerUUID := rCreate.GetResourceUUID()
		taskUUID := rCreate.GetTaskUUID()

		providerutil.WaitForCreateProviderTask(authAPI,
			providerName, providerUUID, providerCode, taskUUID)
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
			"creation will fail on insufficient permissions on the host, defaults to false.")
	createAWSProviderCmd.Flags().String("hosted-zone-id", "",
		"[Optional] Hosted Zone ID corresponding to Amazon Route53.")

	createAWSProviderCmd.Flags().StringArray("region", []string{},
		"[Required] Region associated with the AWS provider. Minimum number of required "+
			"regions = 1. Provide the following comma separated fields as key-value pairs:"+
			"\"region-name=<region-name>,"+
			"vpc-id=<vpc-id>,sg-id=<security-group-id>,arch=<architecture>,yb-image=<custom-ami>\". "+
			formatter.Colorize("Region name is required key-value.",
				formatter.GreenColor)+
			" VPC ID, Security Group ID, YB Image (AMI) and Architecture"+
			" (Default to x86_84) are optional. "+
			"Each region needs to be added using a separate --region flag. "+
			"Example: --region region-name=us-west-2,vpc-id=<vpc-id>,sg-id=<security-group> "+
			"--region region-name=us-east-2,vpc-id=<vpc-id>,sg-id=<security-group>")
	createAWSProviderCmd.Flags().StringArray("zone", []string{},
		"[Required] Zone associated to the AWS Region defined. "+
			"Provide the following comma separated fields as key-value pairs:"+
			"\"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>,"+
			"secondary-subnet=<secondary-subnet-id>\". "+
			formatter.Colorize("Zone name, Region name and subnet IDs are required values. ",
				formatter.GreenColor)+
			"Secondary subnet ID is optional. Each --region definition "+
			"must have atleast one corresponding --zone definition. Multiple --zone definitions "+
			"can be provided per region."+
			"Each zone needs to be added using a separate --zone flag. "+
			"Example: --zone zone-name=us-west-2a,region-name=us-west-2,subnet=<subnet-id>"+
			" --zone zone-name=us-west-2b,region-name=us-west-2,subnet=<subnet-id>")

	createAWSProviderCmd.Flags().String("ssh-user", "",
		"[Optional] SSH User to access the YugabyteDB nodes.")
	createAWSProviderCmd.Flags().Int("ssh-port", 22,
		"[Optional] SSH Port to access the YugabyteDB nodes.")
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

	createAWSProviderCmd.Flags().Bool("airgap-install", false,
		"[Optional] Are YugabyteDB nodes installed in an air-gapped environment,"+
			" lacking access to the public internet for package downloads, "+
			"defaults to false.")
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
						YbImage:         util.GetStringPointer(region["yb-image"]),
						Arch:            util.GetStringPointer(region["arch"]),
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
