/*
 * Copyright (c) YugaByte, Inc.
 */

package pitr

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var createPITRCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a new PITR configuration for the universe",
	Long:    "Create Point-In-Time Recovery (PITR) configuration for a keyspace in the universe",
	Example: `yba backup pitr create --universe-name <universe-name> --keyspace <keyspace-name>
	--table-type <table-type> --retention-period <retention-period>`,
	Run: func(cmd *cobra.Command, args []string) {
		universeName := util.MustGetFlagString(cmd, "universe-name")
		keyspaceName := util.MustGetFlagString(cmd, "keyspace")
		tableType := util.MustGetFlagString(cmd, "table-type")
		if strings.EqualFold(tableType, "ysql") {
			tableType = util.YSQLWorkloadType
		} else if strings.EqualFold(tableType, "ycql") {
			tableType = util.YCQLWorkloadType
		} else {
			logrus.Fatalln(
				formatter.Colorize(
					fmt.Sprintf("Table type provided %s is not supported.", tableType),
					formatter.RedColor,
				),
			)
		}
		retentionInSecs := util.MustGetFlagInt64(cmd, "retention-in-secs")
		scheduleIntervalInSecs := util.MustGetFlagInt64(cmd, "schedule-interval-in-secs")
		if scheduleIntervalInSecs <= 0 {
			logrus.Fatalln(
				formatter.Colorize(
					"Schedule interval should be greater than 0.",
					formatter.RedColor,
				),
			)
		}
		if retentionInSecs <= scheduleIntervalInSecs {
			logrus.Fatalln(
				formatter.Colorize(
					fmt.Sprintf(
						"Retention period should be greater than %d.",
						scheduleIntervalInSecs,
					),
					formatter.RedColor,
				),
			)
		}
		CreatePITRUtil(
			cmd,
			universeName,
			keyspaceName,
			tableType,
			retentionInSecs,
			scheduleIntervalInSecs,
		)
	},
}

func init() {
	createPITRCmd.Flags().SortFlags = false
	createPITRCmd.Flags().StringP("keyspace", "k", "",
		"[Required] The name of the keyspace for which PITR config needs to be created.")
	createPITRCmd.MarkFlagRequired("keyspace")
	createPITRCmd.Flags().StringP("table-type", "t", "",
		"[Required] The table type for which PITR config needs to be created. "+
			"Supported values: ycql, ysql")
	createPITRCmd.MarkFlagRequired("table-type")
	createPITRCmd.Flags().Int64P("retention-in-secs", "r", 0,
		"[Required] The retention period in seconds for the PITR config.")
	createPITRCmd.MarkFlagRequired("retention-in-secs")
	createPITRCmd.Flags().Int64P("schedule-interval-in-secs", "s", 86400,
		"[Optional] The schedule interval in seconds for the PITR config.")
}
