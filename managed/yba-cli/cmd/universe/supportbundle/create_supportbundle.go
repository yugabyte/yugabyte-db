/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundle

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/supportbundle"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

var createSupportBundleUniverseCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a support bundle for a YugabyteDB Anywhere universe",
	Long:    "Create a support bundle for a YugabyteDB Anywhere universe",
	Example: `yba universe support-bundle -n dkumar-cli create \
  --components "UniverseLogs, OutputFiles, ErrorFiles, SystemLogs, PrometheusMetrics, ApplicationLogs" \
  --start-time 2025-04-17T00:00:00Z --end-time 2025-04-17T23:59:59Z`,
	PreRun: func(cmd *cobra.Command, args []string) {
		_ = util.MustGetFlagString(cmd, "name")
		components, err := cmd.Flags().GetString("components")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if components == "" {
			logrus.Fatalf(
				formatter.Colorize(
					"List of components is required to create support bundle\n",
					formatter.RedColor,
				),
			)
		}
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.SupportBundleOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.SupportBundleOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		supportbundle.Universe = universe

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		componentsString, err := cmd.Flags().GetString("components")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		componentsString = strings.Trim(componentsString, " ")
		components := strings.Split(componentsString, ",")
		for i, component := range components {
			component = strings.TrimSpace(component)
			components[i] = component
		}

		startTimeString, err := cmd.Flags().GetString("start-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		startTime, err := time.Parse(time.RFC3339, startTimeString)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		endTimeString, err := cmd.Flags().GetString("end-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		endTime, err := time.Parse(time.RFC3339, endTimeString)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if endTime.Before(startTime) {
			logrus.Fatalf(
				formatter.Colorize("End time must be after start time\n", formatter.RedColor),
			)
		}

		maxRecentCores, err := cmd.Flags().GetInt("max-recent-cores")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		maxCoreFileSize, err := cmd.Flags().GetInt64("max-core-file-size")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		promStartTimeString, err := cmd.Flags().GetString("prom-dump-start-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		promEndTimeString, err := cmd.Flags().GetString("prom-dump-end-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		promMetricTypesString, err := cmd.Flags().GetString("prom-metric-types")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		promMetricTypes := make([]string, 0)
		if promMetricTypesString != "" {
			promMetricTypesString = strings.Trim(promMetricTypesString, " ")
			promMetricTypes = strings.Split(promMetricTypesString, ",")
			for i, metricType := range promMetricTypes {
				metricType = strings.TrimSpace(metricType)
				if metricType != "master_export" && metricType != "node_export" &&
					metricType != "platform" && metricType != "prometheus" &&
					metricType != "tserver_export" && metricType != "cql_export" &&
					metricType != "ysql_export" {
					logrus.Fatalf(
						formatter.Colorize(
							"Invalid prometheus metric type: "+metricType+"\n",
							formatter.RedColor,
						),
					)
				} else {
					metricType = strings.ToUpper(metricType)
					promMetricTypes[i] = metricType
				}
			}
		}

		var promQueriesMap map[string]string

		promQueriesString, err := cmd.Flags().GetString("prom-queries")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(promQueriesString) {
			filePath, err := cmd.Flags().GetString("prom-queries-file-path")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(filePath) {
				fileByte, err := os.ReadFile(filePath)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				promQueriesString = string(fileByte)
			}
		}

		if !util.IsEmptyString(promQueriesString) {
			err = json.Unmarshal([]byte(promQueriesString), &promQueriesMap)
			if err != nil {
				logrus.Fatal(
					formatter.Colorize(
						"Error unmarshaling prometheus queries: "+err.Error()+"\n",
						formatter.RedColor))
			}
		} else {
			promQueriesMap = make(map[string]string, 0)
		}

		requestBody := ybaclient.SupportBundleFormData{
			Components:             components,
			StartDate:              startTime,
			EndDate:                endTime,
			MaxNumRecentCores:      util.GetInt32Pointer(int32(maxRecentCores)),
			MaxCoreFileSize:        util.GetInt64Pointer(maxCoreFileSize),
			PrometheusMetricsTypes: promMetricTypes,
			PromQueries:            &promQueriesMap,
		}

		if promStartTimeString != "" {
			promStartTime, err := time.Parse(time.RFC3339, promStartTimeString)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			requestBody.SetPromDumpStartDate(promStartTime)
		} else {
			requestBody.PromDumpStartDate = nil
		}
		if promEndTimeString != "" {
			promEndTime, err := time.Parse(time.RFC3339, promEndTimeString)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			requestBody.SetPromDumpEndDate(promEndTime)
		} else {
			requestBody.PromDumpEndDate = nil
		}

		rTask, response, err := authAPI.CreateSupportBundle(universeUUID).
			SupportBundle(requestBody).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe: Support Bundle", "Create")
		}

		util.CheckTaskAfterCreation(rTask)

		taskUUID := rTask.GetTaskUUID()
		bundleUUID := rTask.GetResourceUUID()

		msg := fmt.Sprintf("The support bundle %s for universe %s (%s) is being created",
			formatter.Colorize(bundleUUID, formatter.GreenColor), universeName, universeUUID)

		if viper.GetBool("wait") {
			if taskUUID != "" {
				logrus.Info(
					fmt.Sprintf(
						"\nWaiting for support bundle %s for universe %s (%s) to be created\n",
						formatter.Colorize(
							bundleUUID,
							formatter.GreenColor,
						),
						universeName,
						universeUUID,
					),
				)
				err = authAPI.WaitForTask(taskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The support bundle %s for universe %s (%s) has been created\n",
				formatter.Colorize(bundleUUID, formatter.GreenColor), universeName, universeUUID)

			bundle, response, err := authAPI.GetSupportBundle(universeUUID, bundleUUID).Execute()
			if err != nil {
				util.FatalHTTPError(
					response,
					err,
					"Universe: Support Bundle",
					"Create - Fetch Support Bundle",
				)
			}

			r := util.CheckAndAppend(
				make([]ybaclient.SupportBundle, 0),
				bundle,
				fmt.Sprintf("Support Bundle %s for universe %s (%s) not found",
					bundleUUID,
					universeName,
					universeUUID,
				),
			)

			supportBundleCtx := formatter.Context{
				Command: "create",
				Output:  os.Stdout,
				Format:  supportbundle.NewSupportBundleFormat(viper.GetString("output")),
			}

			supportbundle.Write(supportBundleCtx, r)
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})

	},
}

func init() {
	createSupportBundleUniverseCmd.Flags().SortFlags = false

	createSupportBundleUniverseCmd.Flags().String("components", "",
		"[Required] Comman separated list of components to include in the support bundle. "+
			"Run \"yba universe support-bundle list-components\" to check list of case sensitive allowed components.")
	createSupportBundleUniverseCmd.MarkFlagRequired("components")
	createSupportBundleUniverseCmd.Flags().
		String("start-time", "",
			"[Required] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for start time of the support bundle.")
	createSupportBundleUniverseCmd.MarkFlagRequired("start-time")
	createSupportBundleUniverseCmd.Flags().
		String("end-time", "",
			"[Required] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for end time of the support bundle.")
	createSupportBundleUniverseCmd.MarkFlagRequired("end-time")

	createSupportBundleUniverseCmd.Flags().
		Int("max-recent-cores", 1, "[Optional] Maximum number of most recent cores to include in the support bundle if any.")
	createSupportBundleUniverseCmd.Flags().
		Int64("max-core-file-size", 25000000000, "[Optional] Maximum size in bytes of the recent collected cores if any.")

	createSupportBundleUniverseCmd.Flags().
		String("prom-dump-start-time", "",
			fmt.Sprintf("[Optional] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for start time to filter prometheus metrics. %s."+
				" If not provided, the last 15 mins of prometheus metrics from end-time will be collected.",
				formatter.Colorize(
					"Required with --prom-dump-end-time flag",
					formatter.GreenColor,
				)),
		)
	createSupportBundleUniverseCmd.Flags().
		String("prom-dump-end-time", "",
			fmt.Sprintf("[Optional] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for end time to filter prometheus metrics. %s."+
				" If not provided, the last 15 mins of prometheus metrics from end-time will be collected.",
				formatter.Colorize(
					"Required with --prom-dump-start-time flag",
					formatter.GreenColor,
				)),
		)

	createSupportBundleUniverseCmd.MarkFlagsRequiredTogether(
		"prom-dump-start-time",
		"prom-dump-end-time",
	)

	createSupportBundleUniverseCmd.Flags().
		String("prom-metric-types", "",
			"[Optional] Comma separated list of prometheus metric types to include in the support bundle. "+
				"Allowed values: master_export, node_export, platform, prometheus, tserver_export, cql_export, ysql_export",
		)

	createSupportBundleUniverseCmd.Flags().String("prom-queries", "",
		fmt.Sprintf("[Optional] Prometheus queries in JSON map format to include in the support bundle. "+
			"Provide the prometheus queries in the following format: "+
			"\"--prom-queries '{\"key-1\":\"value-1\","+
			"\"key-2\":\"value-2\"}'\" where the key is the name of the custom query and the value is the query. %s",
			formatter.Colorize(
				"Provide either prom-queries or prom-queries-file-path", formatter.GreenColor,
			),
		),
	)

	createSupportBundleUniverseCmd.Flags().String("prom-queries-file-path", "",
		fmt.Sprintf(
			"[Optional] Path to a file containing prometheus queries in JSON map format to include in the support bundle. %s",
			formatter.Colorize(
				"Provide either prom-queries or prom-queries-file-path", formatter.GreenColor,
			),
		),
	)

	createSupportBundleUniverseCmd.MarkFlagsMutuallyExclusive(
		"prom-queries",
		"prom-queries-file-path",
	)

}
