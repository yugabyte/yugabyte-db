/*
 * Copyright (c) YugabyteDB, Inc.
 */

package endpoint

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/perfadvisor"
)

// EndpointCmd set of commands manage the external Perf Advisor destinations a
// universe registered in online mode forwards its collected data to.
var EndpointCmd = &cobra.Command{
	Use:     "endpoint",
	Aliases: []string{"endpoints", "ep"},
	Short:   "Manage YugabyteDB Anywhere Perf Advisor endpoints",
	Long: "Manage the external Perf Advisor destinations that universes " +
		"registered in online mode forward their collected data to. Requires " +
		"Perf Advisor online mode to be enabled for the customer.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	EndpointCmd.AddCommand(listEndpointCmd)
	EndpointCmd.AddCommand(describeEndpointCmd)
	EndpointCmd.AddCommand(createEndpointCmd)
	EndpointCmd.AddCommand(updateEndpointCmd)
	EndpointCmd.AddCommand(deleteEndpointCmd)
}

// listEndpoints fetches every endpoint, optionally narrowed to one name.
func listEndpoints(
	authAPI *ybaAuthClient.AuthAPIClient, name string,
) []ybav2client.PerfAdvisorEndpoint {
	endpoints, response, err := authAPI.ListPerfAdvisorEndpoints().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Perf Advisor Endpoint", "List")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	if name == "" {
		return endpoints
	}
	matched := make([]ybav2client.PerfAdvisorEndpoint, 0, 1)
	for _, endpoint := range endpoints {
		if endpoint.GetSpec().Name == name {
			matched = append(matched, endpoint)
		}
	}
	return matched
}

// findEndpoint resolves the --name flag to exactly one endpoint, because every
// command other than list and create operates on a UUID the user does not type.
func findEndpoint(
	authAPI *ybaAuthClient.AuthAPIClient, name string,
) ybav2client.PerfAdvisorEndpoint {
	matched := listEndpoints(authAPI, name)
	if len(matched) < 1 {
		logrus.Fatalf("%s", formatter.Colorize(
			"No Perf Advisor endpoint named \""+name+"\" found\n", formatter.RedColor))
	}
	return matched[0]
}

func writeEndpointDetails(endpoint ybav2client.PerfAdvisorEndpoint) {
	fullCtx := perfadvisor.NewFullEndpointContext()
	fullCtx.Output = os.Stdout
	fullCtx.Format = perfadvisor.NewFullEndpointFormat(viper.GetString("output"))
	fullCtx.SetFullEndpoint(endpoint)
	fullCtx.Write()
}
