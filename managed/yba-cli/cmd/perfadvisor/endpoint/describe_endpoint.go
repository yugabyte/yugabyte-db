/*
 * Copyright (c) YugabyteDB, Inc.
 */

package endpoint

import (
	"github.com/spf13/cobra"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
)

var describeEndpointCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere Perf Advisor endpoint",
	Long:    "Describe an external Perf Advisor destination, including which universes forward to it",
	Example: `yba perf-advisor endpoint describe --name <endpoint-name>`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
		name, _ := cmd.Flags().GetString("name")
		writeEndpointDetails(findEndpoint(authAPI, name))
	},
}

func init() {
	describeEndpointCmd.Flags().SortFlags = false
	describeEndpointCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the Perf Advisor endpoint.")
	describeEndpointCmd.MarkFlagRequired("name")
}
