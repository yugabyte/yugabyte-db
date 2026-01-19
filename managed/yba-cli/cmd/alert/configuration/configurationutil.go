/*
 * Copyright (c) YugabyteDB, Inc.
 */

package configuration

import (
	"fmt"
	"net/http"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	alertconfiguration "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/configuration"
)

func populateAlertDestinationAndTemplates(authAPI *ybaAuthClient.AuthAPIClient, operation string) {
	var err error
	var response *http.Response
	messageOperation := fmt.Sprintf("%s - Get Alert Destinations", operation)
	alertconfiguration.AlertDestinations, response, err = authAPI.ListAlertDestinations().Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "Alert Policy", messageOperation)
	}

	alertconfiguration.Templates, response, err = authAPI.ListAlertTemplates().
		ListTemplatesRequest(ybaclient.AlertTemplateApiFilter{}).
		Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "Alert Policy", messageOperation)
	}
}
