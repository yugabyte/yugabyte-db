/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetypeutil

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
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

	existing, cloudCodes, err := lookupInstanceType(
		authAPI, providerUUID, providerType, instanceTypeName, callSite)
	if err != nil {
		logrus.Fatalln(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if existing != nil && len(cloudCodes) > 0 {
		logrus.Fatalln(formatter.Colorize(
			fmt.Sprintf("Instance type %s maps to cloud instance types %s, which yba-cli "+
				"cannot keep when it updates the type.\n",
				instanceTypeName, strings.Join(cloudCodes, ", ")),
			formatter.RedColor))
	}

	requestBody, err := buildInstanceTypeRequest(
		providerUUID,
		providerType,
		instanceTypeName,
		existing,
		addInstanceTypeFlags{
			memSize:  memSize,
			numCores: numCores,
			tenancy:  tenancy,
			arch:     architecture,
			volumes:  volume,
		},
		cmd.Flags().Changed,
	)
	if err != nil {
		logrus.Fatalln(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
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

	action := "added to"
	if existing != nil {
		action = "updated in"
	}
	logrus.Infof("The instance type %s has been %s provider %s (%s)\n",
		formatter.Colorize(instanceTypeName, formatter.GreenColor),
		action,
		providerName,
		providerUUID)

	instanceTypeList := make([]ybaclient.InstanceTypeResp, 0)
	instanceTypeList = append(instanceTypeList, *rCreate)
	instancetype.Write(instanceTypesCtx, instanceTypeList)
}

// lookupInstanceType returns the active stored instance type, or nil if there is none,
// and its cloudInstanceTypeCodes, which the Go client's InstanceTypeDetails does not
// model and so cannot send back. It fails rather than return nil when the type exists
// but its stored values can't be read, since the caller would then reset it.
func lookupInstanceType(
	authAPI *ybaAuthClient.AuthAPIClient,
	providerUUID, providerType, instanceTypeName, callSite string,
) (*ybaclient.InstanceTypeResp, []string, error) {
	// Look the type up by code: the list API hides AWS types whose arch differs from the
	// default image bundle's, and types with unsupported prefixes.
	existing, response, err := authAPI.InstanceTypeDetail(providerUUID, instanceTypeName).Execute()
	if err == nil {
		if !existing.GetActive() {
			// The create API replaces an inactive type with a new one.
			return nil, nil, nil
		}
		var stored storedInstanceType
		if err := decodeBody(response, &stored); err != nil {
			return nil, nil, err
		}
		return existing, stored.cloudCodes(), nil
	}
	// The provider was found above, so a 400 here means it has no such instance type.
	if response != nil && response.StatusCode == http.StatusBadRequest {
		return nil, nil, nil
	}
	detailErr := util.ErrorFromHTTPResponse(response, err, callSite, "Add - Get Instance Type")
	// The detail API fails with a 500 for a cloud type stored without details or volumes.
	// Outside AWS, the list API returns such a type as stored. On AWS it hides types by
	// arch and prefix and fills in default volumes, so it can't stand in.
	if response == nil || response.StatusCode < http.StatusInternalServerError ||
		strings.EqualFold(providerType, util.AWSProviderType) {
		return nil, nil, detailErr
	}
	instanceTypes, response, err := authAPI.ListOfInstanceType(providerUUID).Execute()
	if err != nil {
		return nil, nil, util.ErrorFromHTTPResponse(
			response, err, callSite, "Add - List Instance Types")
	}
	var stored []storedInstanceType
	if err := decodeBody(response, &stored); err != nil {
		return nil, nil, err
	}
	for i := range instanceTypes {
		if instanceTypes[i].IdKey.InstanceTypeCode != instanceTypeName {
			continue
		}
		for _, s := range stored {
			if s.IdKey.InstanceTypeCode == instanceTypeName {
				return &instanceTypes[i], s.cloudCodes(), nil
			}
		}
	}
	// The detail API answered 5xx, not 400, so the type exists.
	return nil, nil, detailErr
}

type storedInstanceType struct {
	IdKey struct {
		InstanceTypeCode string `json:"instanceTypeCode"`
	} `json:"idKey"`
	InstanceTypeDetails *struct {
		CloudInstanceTypeCodes []string `json:"cloudInstanceTypeCodes"`
	} `json:"instanceTypeDetails"`
}

func (s storedInstanceType) cloudCodes() []string {
	if s.InstanceTypeDetails == nil {
		return nil
	}
	return s.InstanceTypeDetails.CloudInstanceTypeCodes
}

// decodeBody decodes a response body the generated client has already decoded; the
// client puts the body back after reading it.
func decodeBody(response *http.Response, v any) error {
	if response == nil || response.Body == nil {
		return errors.New("could not read the stored instance type: empty response")
	}
	body, err := io.ReadAll(response.Body)
	if err != nil {
		return fmt.Errorf("could not read the stored instance type: %w", err)
	}
	if err := json.Unmarshal(body, v); err != nil {
		return fmt.Errorf("could not read the stored instance type: %w", err)
	}
	return nil
}

type addInstanceTypeFlags struct {
	memSize  float64
	numCores float64
	tenancy  string
	arch     string
	volumes  []string
}

func buildInstanceTypeRequest(
	providerUUID, providerType, instanceTypeName string,
	existing *ybaclient.InstanceTypeResp,
	flags addInstanceTypeFlags,
	changed func(flag string) bool,
) (ybaclient.InstanceType, error) {
	// The create API replaces every field of an existing instance type, so a flag the user
	// did not set must carry the existing value rather than the flag default.
	useFlag := func(flag string) bool {
		return existing == nil || changed(flag)
	}
	details := ybaclient.InstanceTypeDetails{}
	memSize := flags.memSize
	numCores := flags.numCores
	if existing != nil {
		if d := existing.InstanceTypeDetails; d != nil {
			details.Tenancy = d.Tenancy
			details.Arch = d.Arch
			details.VolumeDetailsList = append([]ybaclient.VolumeDetails(nil),
				d.VolumeDetailsList...)
		}
		if !useFlag("mem-size") && existing.MemSizeGB != nil {
			memSize = existing.GetMemSizeGB()
		}
		if !useFlag("num-cores") && existing.NumCores != nil {
			numCores = existing.GetNumCores()
		}
	}
	if useFlag("tenancy") {
		details.Tenancy = util.GetStringPointer(flags.tenancy)
	}
	if useFlag("volume") {
		details.VolumeDetailsList = buildVolumeDetails(flags.volumes, providerType)
	}
	// The server accepts an onprem type without volumes.
	if len(details.VolumeDetailsList) == 0 &&
		strings.EqualFold(providerType, util.OnpremProviderType) {
		return ybaclient.InstanceType{},
			errors.New("at least one volume is required per instance type")
	}
	if !util.IsEmptyString(flags.arch) &&
		(useFlag("arch") || util.IsEmptyString(details.GetArch())) {
		details.SetArch(flags.arch)
	}
	return ybaclient.InstanceType{
		IdKey: ybaclient.InstanceTypeKey{
			InstanceTypeCode: instanceTypeName,
			ProviderUuid:     providerUUID,
		},
		InstanceTypeDetails: &details,
		MemSizeGB:           util.GetFloat64Pointer(memSize),
		NumCores:            util.GetFloat64Pointer(numCores),
	}, nil
}

func buildVolumeDetails(volumeStrings []string, providerType string) []ybaclient.VolumeDetails {
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
