/*
 * Copyright (c) YugabyteDB, Inc.
 */

package providerutil

import (
	"fmt"
	"net/http"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	providerFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/provider"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// WaitForUpdateProviderTask is a util function to monitor update tasks
func WaitForUpdateProviderTask(
	authAPI *ybaAuthClient.AuthAPIClient,
	providerName string, rTask *ybaclient.YBPTask, providerCode string) {

	var providerData []ybaclient.Provider
	var response *http.Response
	var err error

	util.CheckTaskAfterCreation(rTask)

	providerUUID := rTask.GetResourceUUID()
	taskUUID := rTask.GetTaskUUID()

	msg := fmt.Sprintf("The provider %s (%s) is being updated",
		formatter.Colorize(providerName, formatter.GreenColor), providerUUID)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf("Waiting for provider %s (%s) to be updated\n",
				formatter.Colorize(providerName, formatter.GreenColor), providerUUID))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The provider %s (%s) has been updated\n",
			formatter.Colorize(providerName, formatter.GreenColor), providerUUID)

		providerData, response, err = authAPI.GetListOfProviders().
			Name(providerName).ProviderCode(providerCode).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Provider", "Update - Fetch Provider")
		}
		providersCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  providerFormatter.NewProviderFormat(viper.GetString("output")),
		}

		providerFormatter.Write(providersCtx, providerData)
		return
	} else {
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})
	}

}

// BuildZoneMapFromString is to process zone flags
func BuildZoneMapFromString(
	zoneString string,
	operation string,
) map[string]string {
	zone := map[string]string{}
	for _, zoneInfo := range strings.Split(zoneString, util.Separator) {
		kvp := strings.Split(zoneInfo, "=")
		if len(kvp) != 2 {
			logrus.Fatalln(
				formatter.Colorize("Incorrect format in zone description\n",
					formatter.RedColor))
		}
		key := kvp[0]
		val := kvp[1]
		switch key {
		case "zone-name":
			if !util.IsEmptyString(val) {
				zone["name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "region-name":
			if !util.IsEmptyString(val) {
				zone["region-name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "subnet":
			if !util.IsEmptyString(val) {
				zone["subnet"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "secondary-subnet":
			if !util.IsEmptyString(val) {
				zone["secondary-subnet"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "config-file-path":
			if !util.IsEmptyString(val) {
				zone["config-file-path"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "storage-class":
			if !util.IsEmptyString(val) {
				zone["storage-class"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "cert-manager-cluster-issuer":
			if !util.IsEmptyString(val) {
				zone["cert-manager-cluster-issuer"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "cert-manager-issuer":
			if !util.IsEmptyString(val) {
				zone["cert-manager-issuer"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "domain":
			if !util.IsEmptyString(val) {
				zone["domain"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "namespace":
			if !util.IsEmptyString(val) {
				zone["namespace"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "overrides-file-path":
			if !util.IsEmptyString(val) {
				zone["overrides-file-path"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "pod-address-template":
			if !util.IsEmptyString(val) {
				zone["pod-address-template"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "num-nodes":
			if !util.IsEmptyString(val) {
				zone["num-nodes"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		}
	}
	if _, ok := zone["name"]; !ok {
		callsite := ""
		if len(operation) == 0 {
			callsite = "zone"
		} else {
			callsite = fmt.Sprintf("%s-zone", operation)
		}
		logrus.Fatalln(
			formatter.Colorize(
				fmt.Sprintf("Name not specified in %s.\n", callsite),
				formatter.RedColor))
	}
	if _, ok := zone["region-name"]; !ok {
		callsite := ""
		if len(operation) == 0 {
			callsite = "zone"
		} else {
			callsite = fmt.Sprintf("%s-zone", operation)
		}
		logrus.Fatalln(
			formatter.Colorize(
				fmt.Sprintf("Region name not specified in %s.\n", callsite),
				formatter.RedColor))
	}
	return zone
}

// BuildRegionMapFromString is for region flags
func BuildRegionMapFromString(
	regionString, operation string,
) map[string]string {
	region := map[string]string{}
	for _, regionInfo := range strings.Split(regionString, util.Separator) {
		kvp := strings.Split(regionInfo, "=")
		if len(kvp) != 2 {
			logrus.Fatalln(
				formatter.Colorize("Incorrect format in region description.\n",
					formatter.RedColor))
		}
		key := kvp[0]
		val := kvp[1]
		switch key {
		case "region-name":
			if !util.IsEmptyString(val) {
				region["name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "vpc-id":
			if !util.IsEmptyString(val) {
				region["vpc-id"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "sg-id":
			if !util.IsEmptyString(val) {
				region["sg-id"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "arch":
			if !util.IsEmptyString(val) {
				region["arch"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "shared-subnet":
			if !util.IsEmptyString(val) {
				region["shared-subnet"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "instance-template":
			if !util.IsEmptyString(val) {
				region["instance-template"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "vnet":
			if !util.IsEmptyString(val) {
				region["vnet"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "network-rg":
			if !util.IsEmptyString(val) {
				region["network-rg"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "rg":
			if !util.IsEmptyString(val) {
				region["rg"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "latitude":
			if !util.IsEmptyString(val) {
				region["latitude"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "config-file-path":
			if !util.IsEmptyString(val) {
				region["config-file-path"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "storage-class":
			if !util.IsEmptyString(val) {
				region["storage-class"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "cert-manager-cluster-issuer":
			if !util.IsEmptyString(val) {
				region["cert-manager-cluster-issuer"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "cert-manager-issuer":
			if !util.IsEmptyString(val) {
				region["cert-manager-issuer"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "domain":
			if !util.IsEmptyString(val) {
				region["domain"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "namespace":
			if !util.IsEmptyString(val) {
				region["namespace"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "overrides-file-path":
			if !util.IsEmptyString(val) {
				region["overrides-file-path"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "pod-address-template":
			if !util.IsEmptyString(val) {
				region["pod-address-template"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		}
	}
	if _, ok := region["name"]; !ok {
		callsite := ""
		if len(operation) == 0 {
			callsite = "region"
		} else {
			callsite = fmt.Sprintf("%s-zone", operation)
		}
		logrus.Fatalln(
			formatter.Colorize(
				fmt.Sprintf("Name not specified in %s.\n", callsite),
				formatter.RedColor))
	}
	return region
}

// BuildImageBundleMapFromString is to process image bundle flags
func BuildImageBundleMapFromString(
	imageBundleString, operation string,
) map[string]string {
	imageBundle := map[string]string{}
	for _, ibInfo := range strings.Split(imageBundleString, util.Separator) {
		kvp := strings.Split(ibInfo, "=")
		if len(kvp) != 2 {
			logrus.Fatalln(
				formatter.Colorize("Incorrect format in image bundle description.\n",
					formatter.RedColor))
		}
		key := kvp[0]
		val := kvp[1]
		switch key {
		case "image-bundle-name":
			if !util.IsEmptyString(val) {
				imageBundle["name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "image-bundle-uuid":
			if !util.IsEmptyString(val) {
				imageBundle["uuid"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "machine-image":
			if !util.IsEmptyString(val) {
				imageBundle["machine-image"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "arch":
			if !util.IsEmptyString(val) {
				imageBundle["arch"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "ssh-user":
			if !util.IsEmptyString(val) {
				imageBundle["ssh-user"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "ssh-port":
			if !util.IsEmptyString(val) {
				imageBundle["ssh-port"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "imdsv2":
			if !util.IsEmptyString(val) {
				imageBundle["imdsv2"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "default":
			if !util.IsEmptyString(val) {
				imageBundle["default"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		}
	}
	if strings.Compare(operation, "add") == 0 {
		if _, ok := imageBundle["name"]; !ok {
			logrus.Fatalln(
				formatter.Colorize(
					"Name not specified in image bundle.\n",
					formatter.RedColor))
		}
	} else if strings.Compare(operation, "edit") == 0 {
		if _, ok := imageBundle["uuid"]; !ok {
			logrus.Fatalln(
				formatter.Colorize(
					"Image bundle uuid not specified in image bundle.\n",
					formatter.RedColor))
		}
	}

	return imageBundle
}

// BuildImageBundleRegionOverrideMapFromString is to process image bundle flags
func BuildImageBundleRegionOverrideMapFromString(
	regionString, operation string,
) map[string]string {
	override := map[string]string{}
	for _, o := range strings.Split(regionString, util.Separator) {
		kvp := strings.Split(o, "=")
		if len(kvp) != 2 {
			logrus.Fatalln(
				formatter.Colorize("Incorrect format in image bundle description.\n",
					formatter.RedColor))
		}
		key := kvp[0]
		val := kvp[1]
		switch key {
		case "image-bundle-name":
			if !util.IsEmptyString(val) {
				override["name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "image-bundle-uuid":
			if !util.IsEmptyString(val) {
				override["uuid"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "machine-image":
			if !util.IsEmptyString(val) {
				override["machine-image"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "region-name":
			if !util.IsEmptyString(val) {
				override["region-name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		}
	}
	if strings.Compare(operation, "add") == 0 {
		if _, ok := override["name"]; !ok {
			logrus.Fatalln(
				formatter.Colorize(
					"Name not specified in image bundle region override.\n",
					formatter.RedColor))
		}
	} else if strings.Compare(operation, "edit") == 0 {
		if _, ok := override["uuid"]; !ok {
			logrus.Fatalln(
				formatter.Colorize(
					"Image bundle uuid not specified in image bundle region override.\n",
					formatter.RedColor))
		}
	}

	if _, ok := override["region-name"]; !ok {
		logrus.Fatalln(
			formatter.Colorize(
				"Region Name not specified in image bundle region override.\n",
				formatter.RedColor))
	}

	return override
}

// DefaultImageBundleValues is to set default values for image bundle
func DefaultImageBundleValues(imageBundle map[string]string) map[string]string {
	if _, ok := imageBundle["ssh-port"]; !ok {
		imageBundle["ssh-port"] = "22"
	}
	if _, ok := imageBundle["default"]; !ok {
		imageBundle["default"] = "false"
	}
	if _, ok := imageBundle["imdsv2"]; !ok {
		imageBundle["imdsv2"] = "false"
	}
	if _, ok := imageBundle["arch"]; !ok {
		imageBundle["arch"] = util.X86_64
	}
	return imageBundle
}
