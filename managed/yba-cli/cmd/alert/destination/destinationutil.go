/*
 * Copyright (c) YugabyteDB, Inc.
 */

package destination

import (
	"fmt"
	"net/http"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	alertdestination "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/destination"
)

func populateAlertChannels(authAPI *ybaAuthClient.AuthAPIClient, operation string) {
	var err error
	var response *http.Response
	messageOperation := fmt.Sprintf("%s - Get Alert Channels", operation)
	alertdestination.AlertChannels, response, err = authAPI.ListAlertChannels().Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "Alert Destination", messageOperation)
	}
}
