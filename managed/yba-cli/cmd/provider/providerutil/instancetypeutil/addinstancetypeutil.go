/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetypeutil

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/instancetype"
)

// AddInstanceTypeUtil represents the provider command
func AddInstanceTypeUtil(
	cmd *cobra.Command,
	providerType string,
	providerTypeInLog string,
	providerTypeInMessage string,
) {
	callSite := "Instance Type"
	if len(providerTypeInLog) > 0 {
		callSite = fmt.Sprintf("%s: %s", callSite, providerTypeInLog)
	}
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	providerName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	instancetype.ProviderName = providerName

	providerListRequest := authAPI.GetListOfProviders()
	providerListRequest = providerListRequest.Name(providerName).
		ProviderCode(providerType)
	r, response, err := providerListRequest.Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Add - Get Provider")
	}
	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf(
					"No %s providers with name: %s found\n",
					providerTypeInMessage,
					providerName,
				),
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

	architecture := ""
	if strings.EqualFold(providerType, util.AWSProviderType) {
		architecture, err = cmd.Flags().GetString("arch")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		architecture = strings.ToLower(architecture)
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
			VolumeDetailsList: buildVolumeDetails(volume, providerType),
		},
		MemSizeGB: util.GetFloat64Pointer(memSize),
		NumCores:  util.GetFloat64Pointer(numCores),
	}
	if !util.IsEmptyString(architecture) {
		details := requestBody.GetInstanceTypeDetails()
		details.SetArch(architecture)
		requestBody.SetInstanceTypeDetails(details)
	}

	rCreate, response, err := authAPI.CreateInstanceType(providerUUID).
		InstanceType(requestBody).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Add")
	}

	util.CheckAndDereference(
		rCreate,
		fmt.Sprintf("Failed to create instance type %s", instanceTypeName),
	)

	instanceTypesCtx := formatter.Context{
		Command: "add",
		Output:  os.Stdout,
		Format:  instancetype.NewInstanceTypesFormat(viper.GetString("output")),
	}

	logrus.Infof("The instance type %s has been added to provider %s (%s)\n",
		formatter.Colorize(instanceTypeName, formatter.GreenColor),
		providerName,
		providerUUID)

	instanceTypeList := make([]ybaclient.InstanceTypeResp, 0)
	instanceTypeList = append(instanceTypeList, *rCreate)
	instancetype.Write(instanceTypesCtx, instanceTypeList)
}

func buildVolumeDetails(volumeStrings []string, providerType string) []ybaclient.VolumeDetails {
	if len(volumeStrings) == 0 && strings.EqualFold(providerType, util.OnpremProviderType) {
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
				if !util.IsEmptyString(val) {
					volume["mount-points"] = val
				}
			case "size":
				if !util.IsEmptyString(val) {
					volume["size"] = val
				}
			case "type":
				if !util.IsEmptyString(val) {
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
			if strings.EqualFold(providerType, util.AWSProviderType) {
				volume["type"] = "EBS"
			} else {
				volume["type"] = "SSD"
			}
		} else {
			volume["type"] = strings.ToUpper(volume["type"])
		}
		volumeSize, err := strconv.ParseInt(volume["size"], 10, 64)
		if err != nil {
			errMessage := err.Error() +
				" Invalid or missing value provided for 'size'. Setting it to '100'.\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			volumeSize = 100
		}
		r := ybaclient.VolumeDetails{
			MountPath:    volume["mount-points"],
			VolumeType:   strings.ToUpper(volume["type"]),
			VolumeSizeGB: int32(volumeSize),
		}
		res = append(res, r)
	}
	return res
}

// AddAndListInstanceTypeValidations validates the flags for add instance type
func AddAndListInstanceTypeValidations(cmd *cobra.Command, operation string) {
	providerNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(providerNameFlag) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize("No provider name found to "+operation+" instance type"+
				"\n", formatter.RedColor))
	}
}
