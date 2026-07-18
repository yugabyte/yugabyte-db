/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var runSampleAppsUniverseCmd = &cobra.Command{
	Use:     "run-sample-apps",
	Short:   "Get sample apps command for a YugabyteDB Anywhere universe",
	Long:    "Get sample apps command for a universe in YugabyteDB Anywhere",
	Example: `yba universe run-sample-apps --name <universe-name> --workloads <workloads>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No universe name found to run sample apps\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeListRequest := authAPI.ListUniverses()
		universeName, _ := cmd.Flags().GetString("name")
		universeListRequest = universeListRequest.Name(universeName)

		r, response, err := universeListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Run Sample Apps - List Universe")
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", universeName),
					formatter.RedColor,
				))
		}

		if len(r) > 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("Multiple universes with name: %s found\n", universeName),
					formatter.RedColor,
				))

		}

		universe := r[0]
		details := universe.GetUniverseDetails()
		clusters := details.GetClusters()
		if len(clusters) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No clusters found in universe: %s\n", universeName),
					formatter.RedColor,
				))

		}

		userIntent := clusters[0].GetUserIntent()
		providerCode := userIntent.GetProviderType()

		nodes := details.GetNodeDetailsSet()

		if len(nodes) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No nodes found in universe: %s\n", universeName),
					formatter.RedColor,
				))
		}

		nodeList := ""
		for i, node := range nodes {
			port := node.GetYsqlServerRpcPort()
			cloudInfo := node.GetCloudInfo()
			ip := cloudInfo.GetPrivateIp()
			if node.GetState() == "Live" {
				if i == 0 {
					nodeList = fmt.Sprintf("%s:%d", ip, port)
				} else {
					nodeList = fmt.Sprintf("%s,%s:%d", nodeList, ip, port)
				}
			}

		}

		commandSyntax := ""
		if strings.Compare(providerCode, util.K8sProviderType) == 0 {
			commandSyntax = "kubectl run --image=yugabytedb/yb-sample-apps yb-sample-apps --"
		} else {
			commandSyntax = "docker run -d yugabytedb/yb-sample-apps"
		}

		workload, err := cmd.Flags().GetString("workload")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		sampleAppCommand := ""
		app := util.GetAppTypeByCode(workload)

		if strings.Compare(strings.ToUpper(workload), util.YCQLWorkloadType) == 0 {
			sampleAppCommand = universe.GetSampleAppCommandTxt()
		} else {
			sampleAppCommand = fmt.Sprintf(
				"%s --workload %s --nodes %s --username yugabyte --password <your_password>",
				commandSyntax,
				app.Code,
				nodeList)
		}

		logrus.Info("Run sample apps command:\n")
		logrus.Info(formatter.Colorize(fmt.Sprintf("%s\n", sampleAppCommand), formatter.GreenColor))
		logrus.Info("Other options (with default values):\n")
		for _, option := range app.Options {
			for key, value := range option {
				logrus.Infof("--%s %s\n", key, value)
			}
		}
	},
}

func init() {
	runSampleAppsUniverseCmd.Flags().SortFlags = false
	runSampleAppsUniverseCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to run sample apps.")
	runSampleAppsUniverseCmd.MarkFlagRequired("name")
	runSampleAppsUniverseCmd.Flags().String("workload", "",
		"[Required] Type of workloads to run. Allowed values: ysql, ycql")
	runSampleAppsUniverseCmd.MarkFlagRequired("workload")
}
