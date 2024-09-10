/*
 * Copyright (c) YugaByte, Inc.
 *
 * Define the status command and other useful utils for gathering service Status.
 */
package cmd

import (
	"fmt"
	"log"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var statusCmd = &cobra.Command{
	Use: "status",
	Short: "Print the status of service(s) running as " +
		"part of your YugabyteDB Anywhere installation.",
	Long: `
    The status command is used to print out the information corresponding to the
    status of all services related to YugabyteDB Anywhere, or for just a particular service.
    For each service, the status command will print out the name of the service, the version of the
    service, the port the service is associated with, the location of any
    applicable systemd and config files, and the running status of the service
    (active or inactive)`,
	Args:      cobra.MatchAll(cobra.MaximumNArgs(1), cobra.OnlyValidArgs),
	ValidArgs: []string{"postgres", "prometheus", "yb-platform"},
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.Initialize()
		if err != nil {
			log.Fatal("unable to load yba installer state: " + err.Error())
		}
		if state.CurrentStatus != ybactlstate.InstalledStatus {
			log.Fatal("cannot check service status - need installed state got " +
				state.CurrentStatus.String())
		}
		// Print status for given service.
		if len(args) == 1 {
			service, exists := services[args[0]]
			if !exists {
				fmt.Printf("Service %s was not installed\n", args[0])
				return
			}
			status, err := service.Status()
			if err != nil {
				log.Fatal("Failed to get status: " + err.Error())
			}
			common.PrintStatus(status)
		} else {
			// Print status for all services.
			var statuses []common.Status
			for _, name := range serviceOrder {
				status, err := services[name].Status()
				if err != nil {
					log.Fatal("Failed to get status: " + err.Error())
				}
				statuses = append(statuses, status)
			}

			common.PrintStatus(statuses...)
		}
	},
}

func init() {
	rootCmd.AddCommand(statusCmd)
}
