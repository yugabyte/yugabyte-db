/*
 * Copyright (c) YugaByte, Inc.
 */

package auth

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// HostCmd uses user name and password to configure the CLI
var HostCmd = &cobra.Command{
	Use:     "host",
	Short:   "Refer to YugabyteDB Anywhere host details",
	Long:    "Refer to YugabyteDB Anywhere host details",
	Example: "yba host",
	Run: func(cmd *cobra.Command, args []string) {
		hostString := viper.GetString("host")
		if len(strings.TrimSpace(hostString)) == 0 {
			logrus.Fatalln(
				formatter.Colorize(
					"No valid YugabyteDB Anywhere Host detected. "+
						"Run \"yba auth\" or \"yba login\" to authenticate with YugabyteDB Anywhere.\n",
					formatter.RedColor))
		}

		ybaAuthClient.NewAuthAPIClientAndCustomer()
		logrus.Infof("YugabyteDB Anywhere is available on %s at version %s\n",
			hostString,
			ybaAuthClient.GetHostVersion())
	},
}

func init() {
	HostCmd.Flags().SortFlags = false
}
