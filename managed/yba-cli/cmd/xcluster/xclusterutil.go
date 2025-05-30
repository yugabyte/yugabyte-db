/*
 * Copyright (c) YugaByte, Inc.
 */

package xcluster

import (
	"fmt"
	"net/http"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// GetSourceAndTargetXClusterUniverse returns source and target universe
func GetSourceAndTargetXClusterUniverse(
	authAPI *ybaAuthClient.AuthAPIClient,
	sourceUniverseName, targetUniverseName, sourceUniverseUUID, targetUniverseUUID string,
	operation string,
) (ybaclient.UniverseResp, ybaclient.UniverseResp) {
	universeListRequest := authAPI.ListUniverses()

	var sourceUniverse, targetUniverse ybaclient.UniverseResp
	var response *http.Response
	var err error
	if strings.TrimSpace(sourceUniverseName) != "" {
		sourceUniverseList, response, err := universeListRequest.Name(sourceUniverseName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "XCluster",
				fmt.Sprintf("%s - Get Source Universe", operation))
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(sourceUniverseList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", sourceUniverseName),
					formatter.RedColor,
				))
		}
		sourceUniverse = sourceUniverseList[0]
	} else if strings.TrimSpace(sourceUniverseUUID) != "" {
		sourceUniverse, response, err = authAPI.GetUniverse(sourceUniverseUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "XCluster",
				fmt.Sprintf("%s - Get Source Universe", operation))
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
	}

	if strings.TrimSpace(sourceUniverse.GetName()) == "" {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No universes with name: %s found\n", sourceUniverseName),
				formatter.RedColor,
			))
	}

	if strings.TrimSpace(targetUniverseName) != "" {

		targetUniverseList, response, err := universeListRequest.Name(targetUniverseName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "XCluster", fmt.Sprintf("%s - Get Target Universe", operation))
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(targetUniverseList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", targetUniverseName),
					formatter.RedColor,
				))
		}
		targetUniverse = targetUniverseList[0]
	} else if strings.TrimSpace(targetUniverseUUID) != "" {
		targetUniverse, response, err = authAPI.GetUniverse(targetUniverseUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "XCluster", fmt.Sprintf("%s - Get Target Universe", operation))
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
	}

	if strings.TrimSpace(targetUniverse.GetName()) == "" && operation != "Full Copy Tables" {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No universes with name: %s found\n", targetUniverseName),
				formatter.RedColor,
			))
	}

	return sourceUniverse, targetUniverse
}
