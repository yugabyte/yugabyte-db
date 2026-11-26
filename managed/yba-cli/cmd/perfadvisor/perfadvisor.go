/*
 * Copyright (c) YugabyteDB, Inc.
 */

package perfadvisor

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/perfadvisor/collector"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/perfadvisor/endpoint"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/perfadvisor/universe"
)

// PerfAdvisorCmd set of commands are used to manage Perf Advisor collection in
// YugabyteDB Anywhere: the collector that scrapes universes, the external
// destinations their data can be forwarded to, and which universe sends where.
var PerfAdvisorCmd = &cobra.Command{
	Use:     "perf-advisor",
	Aliases: []string{"pa"},
	Short:   "Manage YugabyteDB Anywhere Perf Advisor collection",
	Long: "Manage YugabyteDB Anywhere Perf Advisor collection: the collector, " +
		"the external endpoints universes can forward their collected data to, " +
		"and each universe's registration.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	PerfAdvisorCmd.AddCommand(endpoint.EndpointCmd)
	PerfAdvisorCmd.AddCommand(collector.CollectorCmd)
	PerfAdvisorCmd.AddCommand(universe.UniverseCmd)
}
