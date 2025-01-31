/*
 * Copyright (c) YugaByte, Inc.
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
	providerName string, rTask ybaclient.YBPTask, providerCode string) {

	var providerData []ybaclient.Provider
	var response *http.Response
	var err error

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
			errMessage := util.ErrorFromHTTPResponse(response, err, "Provider", "Update - Fetch Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
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
		ybatask.Write(taskCtx, []ybaclient.YBPTask{rTask})
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
			if len(strings.TrimSpace(val)) != 0 {
				zone["name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "region-name":
			if len(strings.TrimSpace(val)) != 0 {
				zone["region-name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "subnet":
			if len(strings.TrimSpace(val)) != 0 {
				zone["subnet"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "secondary-subnet":
			if len(strings.TrimSpace(val)) != 0 {
				zone["secondary-subnet"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "config-file-path":
			if len(strings.TrimSpace(val)) != 0 {
				zone["config-file-path"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "storage-class":
			if len(strings.TrimSpace(val)) != 0 {
				zone["storage-class"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "cert-manager-cluster-issuer":
			if len(strings.TrimSpace(val)) != 0 {
				zone["cert-manager-cluster-issuer"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "cert-manager-issuer":
			if len(strings.TrimSpace(val)) != 0 {
				zone["cert-manager-issuer"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "domain":
			if len(strings.TrimSpace(val)) != 0 {
				zone["domain"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "namespace":
			if len(strings.TrimSpace(val)) != 0 {
				zone["namespace"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "overrides-file-path":
			if len(strings.TrimSpace(val)) != 0 {
				zone["overrides-file-path"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "pod-address-template":
			if len(strings.TrimSpace(val)) != 0 {
				zone["pod-address-template"] = val
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
			if len(strings.TrimSpace(val)) != 0 {
				region["name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "vpc-id":
			if len(strings.TrimSpace(val)) != 0 {
				region["vpc-id"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "sg-id":
			if len(strings.TrimSpace(val)) != 0 {
				region["sg-id"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "arch":
			if len(strings.TrimSpace(val)) != 0 {
				region["arch"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "shared-subnet":
			if len(strings.TrimSpace(val)) != 0 {
				region["shared-subnet"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "instance-template":
			if len(strings.TrimSpace(val)) != 0 {
				region["instance-template"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "vnet":
			if len(strings.TrimSpace(val)) != 0 {
				region["vnet"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "network-rg":
			if len(strings.TrimSpace(val)) != 0 {
				region["network-rg"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "rg":
			if len(strings.TrimSpace(val)) != 0 {
				region["rg"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "latitude":
			if len(strings.TrimSpace(val)) != 0 {
				region["latitude"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "config-file-path":
			if len(strings.TrimSpace(val)) != 0 {
				region["config-file-path"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "storage-class":
			if len(strings.TrimSpace(val)) != 0 {
				region["storage-class"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "cert-manager-cluster-issuer":
			if len(strings.TrimSpace(val)) != 0 {
				region["cert-manager-cluster-issuer"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "cert-manager-issuer":
			if len(strings.TrimSpace(val)) != 0 {
				region["cert-manager-issuer"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "domain":
			if len(strings.TrimSpace(val)) != 0 {
				region["domain"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "namespace":
			if len(strings.TrimSpace(val)) != 0 {
				region["namespace"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "overrides-file-path":
			if len(strings.TrimSpace(val)) != 0 {
				region["overrides-file-path"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "pod-address-template":
			if len(strings.TrimSpace(val)) != 0 {
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
			if len(strings.TrimSpace(val)) != 0 {
				imageBundle["name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "image-bundle-uuid":
			if len(strings.TrimSpace(val)) != 0 {
				imageBundle["uuid"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "machine-image":
			if len(strings.TrimSpace(val)) != 0 {
				imageBundle["machine-image"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "arch":
			if len(strings.TrimSpace(val)) != 0 {
				imageBundle["arch"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "ssh-user":
			if len(strings.TrimSpace(val)) != 0 {
				imageBundle["ssh-user"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "ssh-port":
			if len(strings.TrimSpace(val)) != 0 {
				imageBundle["ssh-port"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "imdsv2":
			if len(strings.TrimSpace(val)) != 0 {
				imageBundle["imdsv2"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "default":
			if len(strings.TrimSpace(val)) != 0 {
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
			if len(strings.TrimSpace(val)) != 0 {
				override["name"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "image-bundle-uuid":
			if len(strings.TrimSpace(val)) != 0 {
				override["uuid"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "machine-image":
			if len(strings.TrimSpace(val)) != 0 {
				override["machine-image"] = val
			} else {
				ValueNotFoundForKeyError(key)
			}
		case "region-name":
			if len(strings.TrimSpace(val)) != 0 {
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
