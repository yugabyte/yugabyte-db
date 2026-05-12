/*
 * Copyright (c) YugabyteDB, Inc.
 */

package xcluster

import (
	"fmt"
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
	if !util.IsEmptyString(sourceUniverseName) {
		sourceUniverseList, response, err := universeListRequest.Name(sourceUniverseName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "xCluster",
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
	} else if !util.IsEmptyString(sourceUniverseUUID) {

		rSourceUniverse, response, err := authAPI.GetUniverse(sourceUniverseUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "xCluster",
				fmt.Sprintf("%s - Get Source Universe", operation))
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		sourceUniverse = util.CheckAndDereference(
			rSourceUniverse,
			fmt.Sprintf("No source universe found with uuid %s", sourceUniverseUUID),
		)
	}

	if strings.TrimSpace(sourceUniverse.GetName()) == "" {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No universes with name: %s found\n", sourceUniverseName),
				formatter.RedColor,
			))
	}

	if !util.IsEmptyString(targetUniverseName) {

		targetUniverseList, response, err := universeListRequest.Name(targetUniverseName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "xCluster", fmt.Sprintf("%s - Get Target Universe", operation))
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
	} else if !util.IsEmptyString(targetUniverseUUID) {
		rTargetUniverse, response, err := authAPI.GetUniverse(targetUniverseUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "xCluster", fmt.Sprintf("%s - Get Target Universe", operation))
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		targetUniverse = util.CheckAndDereference(
			rTargetUniverse,
			fmt.Sprintf("No target universe found with uuid %s", targetUniverseUUID),
		)
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
