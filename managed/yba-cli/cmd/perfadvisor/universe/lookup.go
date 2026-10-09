/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"github.com/sirupsen/logrus"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// The API works in UUIDs; the CLI works in names. These resolve one to the
// other and fail loudly, so a typo is an error rather than a request against
// something that does not exist.

func universeUUIDFromName(authAPI *ybaAuthClient.AuthAPIClient, name string) string {
	universes, response, err := authAPI.ListUniverses().Name(name).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Perf Advisor Universe", "List")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	if len(universes) < 1 {
		logrus.Fatalf("%s", formatter.Colorize(
			"No universe named \""+name+"\" found\n", formatter.RedColor))
	}
	return universes[0].GetUniverseUUID()
}

func endpointUUIDFromName(authAPI *ybaAuthClient.AuthAPIClient, name string) string {
	endpoints, response, err := authAPI.ListPerfAdvisorEndpoints().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "Perf Advisor Endpoint", "List")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	for _, endpoint := range endpoints {
		if endpoint.GetSpec().Name == name {
			return endpoint.GetInfo().Uuid
		}
	}
	logrus.Fatalf("%s", formatter.Colorize(
		"No Perf Advisor endpoint named \""+name+"\" found\n", formatter.RedColor))
	return ""
}

// soleCollectorUUID returns the collector to register with. Only the embedded
// collector is supported today, so there is nothing to choose between; more
// than one is an ambiguity the CLI refuses to guess at.
func soleCollectorUUID(authAPI *ybaAuthClient.AuthAPIClient) string {
	collectors, response, err := authAPI.ListAllPACollectors().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "Perf Advisor Collector", "List")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	if len(collectors) < 1 {
		logrus.Fatalf("%s", formatter.Colorize(
			"No Perf Advisor collector is configured for this customer\n", formatter.RedColor))
	}
	if len(collectors) > 1 {
		logrus.Fatalf("%s", formatter.Colorize(
			"More than one Perf Advisor collector is configured; this command "+
				"supports a single collector\n", formatter.RedColor))
	}
	return collectors[0].GetUuid()
}
