/*
 * Copyright (c) YugaByte, Inc.
 */

package upgrade

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	universeFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
	"gopkg.in/yaml.v2"
)

func waitForUpgradeUniverseTask(
	authAPI *ybaAuthClient.AuthAPIClient, universeName, universeUUID, taskUUID string) {

	var universeData []ybaclient.UniverseResp
	var response *http.Response
	var err error

	msg := fmt.Sprintf("The universe %s (%s) is being upgraded",
		formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf("Waiting for universe %s (%s) to be upgraded\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The universe %s (%s) has been upgraded\n",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

		universeData, response, err = authAPI.ListUniverses().Name(universeName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Upgrade - Fetch Universe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		universesCtx := formatter.Context{
			Output: os.Stdout,
			Format: universeFormatter.NewUniverseFormat(viper.GetString("output")),
		}

		universeFormatter.Write(universesCtx, universeData)

	} else {
		logrus.Infoln(msg + "\n")
	}

}

// Validations to ensure that the universe being accessed exists
func Validations(cmd *cobra.Command, operation string) (
	*ybaAuthClient.AuthAPIClient,
	ybaclient.UniverseResp,
	error,
) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	universeName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	universeListRequest := authAPI.ListUniverses()
	universeListRequest = universeListRequest.Name(universeName)

	r, response, err := universeListRequest.Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err,
			"Universe",
			fmt.Sprintf("%s - List Universes", operation))
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	if len(r) < 1 {
		err := fmt.Errorf("No universes with name: %s found\n", universeName)
		return nil, ybaclient.UniverseResp{}, err
	}
	return authAPI, r[0], nil
}

// FetchMasterGFlags is to fetch list of master gflags
func FetchMasterGFlags(masterGFlagsString string) map[string]string {
	masterGFlags := make(map[string]interface{}, 0)
	if len(strings.TrimSpace(masterGFlagsString)) != 0 {
		for _, masterGFlagPair := range strings.Split(masterGFlagsString, ",") {
			kvp := strings.Split(masterGFlagPair, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in master gflag.\n",
						formatter.RedColor))
			}
			masterGFlags[kvp[0]] = kvp[1]
		}
	}
	return *util.StringMap(masterGFlags)
}

// FetchTServerGFlags is to fetch list of tserver gflags
func FetchTServerGFlags(
	tserverGFlagsStringList []string,
	noOfClusters int,
) []map[string]string {
	tserverGFlagsList := make([]map[string]string, 0)
	for _, tserverGFlagsString := range tserverGFlagsStringList {
		if len(strings.TrimSpace(tserverGFlagsString)) > 0 {
			tserverGFlags := make(map[string]interface{}, 0)
			for _, tserverGFlagPair := range strings.Split(tserverGFlagsString, ",") {
				kvp := strings.Split(tserverGFlagPair, "=")
				if len(kvp) != 2 {
					logrus.Fatalln(
						formatter.Colorize("Incorrect format in tserver gflag.\n",
							formatter.RedColor))
				}
				tserverGFlags[kvp[0]] = kvp[1]
			}
			tserverGflagsMap := util.StringMap(tserverGFlags)
			tserverGFlagsList = append(tserverGFlagsList, *tserverGflagsMap)
		}
	}
	if len(tserverGFlagsList) == 0 {
		for i := 0; i < noOfClusters; i++ {
			tserverGFlagsList = append(tserverGFlagsList, make(map[string]string, 0))
		}
	}
	tserverGFlagsListLen := len(tserverGFlagsList)
	if tserverGFlagsListLen < noOfClusters {
		for i := 0; i < noOfClusters-tserverGFlagsListLen; i++ {
			tserverGFlagsList = append(tserverGFlagsList, tserverGFlagsList[0])
		}
	}
	return tserverGFlagsList
}

// ProcessMasterGflagsJSONString takes in a JSON string and returns it as a map
func ProcessMasterGflagsJSONString(jsonData string) map[string]string {
	// Parse the JSON input into a map
	var singleMap map[string]string
	if err := json.Unmarshal([]byte(jsonData), &singleMap); err != nil {
		logrus.Fatalf(formatter.Colorize(
			fmt.Sprintln("Error parsing JSON:", err), formatter.RedColor))
	}
	logrus.Debug("Master GFlags from JSON string: ", singleMap)
	return singleMap
}

// ProcessMasterGflagsYAMLString takes in a YAML string and returns it as a map
func ProcessMasterGflagsYAMLString(yamlData string) map[string]string {
	// Parse the YAML input into a map
	var singleMap map[string]string
	if err := yaml.Unmarshal([]byte(yamlData), &singleMap); err != nil {
		logrus.Fatalf(formatter.Colorize(
			fmt.Sprintln("Error parsing YAML:", err), formatter.RedColor))
	}
	logrus.Debug("Master GFlags from YAML string: ", singleMap)
	return singleMap
}

// ProcessTServerGFlagsFromString takes in a string and returns it as a map
func ProcessTServerGFlagsFromString(input string, data interface{}) error {
	if err := json.Unmarshal([]byte(input), data); err == nil {
		logrus.Debug("Tserver GFlags from JSON string: ", data)
		return nil
	}

	if err := yaml.Unmarshal([]byte(input), data); err == nil {
		logrus.Debug("Tserver GFlags from YAML string: ", data)
		return nil
	}

	return fmt.Errorf("TServer GFlags is neither valid JSON nor valid YAML")
}

// ProcessTServerGFlagsFromConfig takes in a map from config file and returns it as a map
func ProcessTServerGFlagsFromConfig(input map[string]interface{}) map[string]map[string]string {
	data := make(map[string]map[string]string, 0)
	for k, v := range input {
		data[k] = *util.StringMap(v.(map[string]interface{}))
	}
	return data
}
