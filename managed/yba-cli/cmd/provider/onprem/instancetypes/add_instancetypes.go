/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetypes

import (
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/onprem/instancetypes"
)

// addInstanceTypesCmd represents the provider command
var addInstanceTypesCmd = &cobra.Command{
	Use:     "add",
	Aliases: []string{"create"},
	Short:   "Add an instance type to YugabyteDB Anywhere on-premises provider",
	Long:    "Add an instance type to YugabyteDB Anywhere on-premises provider",
	Example: `yba provider onprem instance-type add \
	--name <provider-name> --instance-type-name <instance-type>\
	--volume mount-points=<mount-point>::size=<size>::type=<volume-type>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(providerNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No provider name found to add instance type"+
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
		providerListRequest = providerListRequest.Name(providerName).ProviderCode(util.OnpremProviderType)
		r, response, err := providerListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Instance Type", "Add - Get Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No on premises providers with name: %s found\n", providerName),
					formatter.RedColor,
				))
		}

		providerUUID := r[0].GetUuid()

		instanceTypeName, err := cmd.Flags().GetString("instance-type-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		memSize, err := cmd.Flags().GetFloat64("mem-size")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		numCores, err := cmd.Flags().GetFloat64("num-cores")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tenancy, err := cmd.Flags().GetString("tenancy")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		volume, err := cmd.Flags().GetStringArray("volume")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.InstanceType{
			IdKey: ybaclient.InstanceTypeKey{
				InstanceTypeCode: instanceTypeName,
				ProviderUuid:     providerUUID,
			},
			InstanceTypeDetails: &ybaclient.InstanceTypeDetails{
				Tenancy:           util.GetStringPointer(tenancy),
				VolumeDetailsList: buildVolumeDetails(volume),
			},
			MemSizeGB: util.GetFloat64Pointer(memSize),
			NumCores:  util.GetFloat64Pointer(numCores),
		}

		rCreate, response, err := authAPI.CreateInstanceType(providerUUID).
			InstanceType(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Instance Type", "Add")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		instanceTypesCtx := formatter.Context{
			Command: "add",
			Output:  os.Stdout,
			Format:  instancetypes.NewInstanceTypesFormat(viper.GetString("output")),
		}

		logrus.Infof("The instance type %s has been added to provider %s (%s)\n",
			formatter.Colorize(instanceTypeName, formatter.GreenColor),
			providerName,
			providerUUID)

		instanceTypeList := make([]ybaclient.InstanceTypeResp, 0)
		instanceTypeList = append(instanceTypeList, rCreate)
		instancetypes.Write(instanceTypesCtx, instanceTypeList)

	},
}

func init() {
	addInstanceTypesCmd.Flags().SortFlags = false

	addInstanceTypesCmd.Flags().String("instance-type-name", "",
		"[Required] Instance type name.")
	addInstanceTypesCmd.Flags().StringArray("volume", []string{},
		"[Required] Volumes associated per node of an instance type. Minimum number of required "+
			"volumes = 1. Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"type=<volume-type>::"+
			"size=<volume-size>::mount-points=<comma-separated-mount-points>\". "+
			formatter.Colorize("Mount points is a required key-value.",
				formatter.GreenColor)+
			" Volume type (Defaults to SSD, Allowed values: EBS, SSD, HDD, NVME)"+
			" and Volume size (Defaults to 100) are optional. "+
			"Each volume needs to be added using a separate --volume flag.")
	addInstanceTypesCmd.Flags().Float64("mem-size", 8,
		"[Optional] Memory size of the node in GB.")
	addInstanceTypesCmd.Flags().Float64("num-cores", 4,
		"[Optional] Number of cores per node.")
	addInstanceTypesCmd.Flags().String("tenancy", "",
		"[Optional] Tenancy of the nodes of this type. Allowed values (case sensitive): "+
			"Shared, Dedicated, Host.")

	addInstanceTypesCmd.MarkFlagRequired("instance-type-name")

}

func buildVolumeDetails(volumeStrings []string) *[]ybaclient.VolumeDetails {
	if len(volumeStrings) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one volume is required per instance type.\n",
				formatter.RedColor))
	}
	res := make([]ybaclient.VolumeDetails, 0)
	for _, volumeString := range volumeStrings {
		volume := map[string]string{}
		for _, volumeInfo := range strings.Split(volumeString, util.Separator) {
			kvp := strings.Split(volumeInfo, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in volume description.\n",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "mount-points":
				if len(strings.TrimSpace(val)) != 0 {
					volume["mount-points"] = val
				}
			case "size":
				if len(strings.TrimSpace(val)) != 0 {
					volume["size"] = val
				}
			case "type":
				if len(strings.TrimSpace(val)) != 0 {
					volume["type"] = val
				}
			}
		}
		if _, ok := volume["mount-points"]; !ok {
			logrus.Fatalln(
				formatter.Colorize("Mount points not specified in volume.\n",
					formatter.RedColor))
		}
		if _, ok := volume["size"]; !ok {
			volume["size"] = "100"
		}
		if _, ok := volume["type"]; !ok {
			volume["type"] = "SSD"
		}
		volumeSize, err := strconv.ParseInt(volume["size"], 10, 64)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		r := ybaclient.VolumeDetails{
			MountPath:    volume["mount-points"],
			VolumeType:   strings.ToUpper(volume["type"]),
			VolumeSizeGB: int32(volumeSize),
		}
		res = append(res, r)
	}
	return &res
}
