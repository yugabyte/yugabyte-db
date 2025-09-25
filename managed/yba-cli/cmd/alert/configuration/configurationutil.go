/*
 * Copyright (c) YugabyteDB, Inc.
 */

package configuration

import (
	"fmt"
	"net/http"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/configuration"
)

func populateAlertDestinationAndTemplates(authAPI *ybaAuthClient.AuthAPIClient, operation string) {
	var err error
	var response *http.Response
	messageOperation := fmt.Sprintf("%s - Get Alert Destinations", operation)
	configuration.AlertDestinations, response, err = authAPI.ListAlertDestinations().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "Alert Policy", messageOperation)
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	configuration.Templates, response, err = authAPI.ListAlertTemplates().
		ListTemplatesRequest(ybaclient.AlertTemplateApiFilter{}).
		Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "Alert Policy", messageOperation)
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
}
