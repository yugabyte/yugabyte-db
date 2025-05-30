/*
 * Copyright (c) YugaByte, Inc.
 */

package destination

import (
	"fmt"
	"net/http"

	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/destination"
)

func populateAlertChannels(authAPI *ybaAuthClient.AuthAPIClient, operation string) {
	var err error
	var response *http.Response
	messageOperation := fmt.Sprintf("%s - Get Alert Channels", operation)
	destination.AlertChannels, response, err = authAPI.ListAlertChannels().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "Alert Destination", messageOperation)
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
}
