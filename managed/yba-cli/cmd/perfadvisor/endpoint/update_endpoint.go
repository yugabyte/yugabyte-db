/*
 * Copyright (c) YugabyteDB, Inc.
 */

package endpoint

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var updateEndpointCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere Perf Advisor endpoint",
	Long: "Update an external Perf Advisor destination. The new settings are " +
		"probed before they are stored, then pushed to every collector already " +
		"using this endpoint, so universes already forwarding here pick them up " +
		"without waiting for the next sync.",
	Example: `yba perf-advisor endpoint update --name byoc-prod --password <new-password>`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, _ := cmd.Flags().GetString("name")
		existing := findEndpoint(authAPI, name)
		current := existing.GetSpec()

		// The API replaces the whole record, so the stored spec is the base and
		// only the flags that were passed override it.
		spec := endpointSpecFromFlags(cmd, &current)
		updated, response, err := authAPI.
			EditPerfAdvisorEndpoint(existing.GetInfo().Uuid).
			PerfAdvisorEndpointSpec(spec).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Perf Advisor Endpoint", "Update")
			logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
		}

		logrus.Infof("Updated Perf Advisor endpoint %s\n",
			formatter.Colorize(spec.Name, formatter.GreenColor))
		writeEndpointDetails(*updated)
	},
}

func init() {
	updateEndpointCmd.Flags().SortFlags = false
	addEndpointFlags(updateEndpointCmd)
	updateEndpointCmd.MarkFlagRequired("name")
}
