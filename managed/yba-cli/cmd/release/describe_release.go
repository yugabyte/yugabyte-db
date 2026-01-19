/*
 * Copyright (c) YugabyteDB, Inc.
 */

package release

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/release/releaseutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/release"
)

var describeReleaseCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB version",
	Long:    "Describe a version of YugabyteDB",
	Example: `yba yb-db-version describe --version <version>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		versionFlag, err := cmd.Flags().GetString("version")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(versionFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No YugabyteDB version found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		releaseutil.IsCommandAllowed(authAPI)

		version, err := cmd.Flags().GetString("version")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		releasesListRequest := authAPI.ListNewReleases()

		deploymentType, err := cmd.Flags().GetString("deployment-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(deploymentType) {
			releasesListRequest = releasesListRequest.DeploymentType(
				strings.ToLower(deploymentType))
		}

		rList, response, err := releasesListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "List")
		}

		requestedRelease := make([]ybaclient.ResponseRelease, 0)
		releaseUUID := ""
		for _, v := range rList {
			if strings.Compare(v.GetVersion(), version) == 0 {
				requestedRelease = append(requestedRelease, v)
				break
			}
		}

		if len(requestedRelease) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No YugabyteDB version: %s found\n", version),
					formatter.RedColor,
				))
		}

		releaseUUID = requestedRelease[0].GetReleaseUuid()

		releaseResponse, response, err := authAPI.GetNewRelease(releaseUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "Describe")
		}

		r := util.CheckAndAppend(
			make([]ybaclient.ResponseRelease, 0),
			releaseResponse,
			fmt.Sprintf("No YugabyteDB version: %s found", version),
		)

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullReleaseContext := *release.NewFullReleaseContext()
			fullReleaseContext.Output = os.Stdout
			fullReleaseContext.Format = release.NewFullReleaseFormat(viper.GetString("output"))
			fullReleaseContext.SetFullRelease(r[0])
			fullReleaseContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No YugabyteDB version: %s found\n", version),
					formatter.RedColor,
				))
		}

		releaseCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  release.NewReleaseFormat(viper.GetString("output")),
		}
		release.Write(releaseCtx, r)

	},
}

func init() {
	describeReleaseCmd.Flags().SortFlags = false
	describeReleaseCmd.Flags().StringP("version", "v", "",
		"[Required] The version to be described.")
	describeReleaseCmd.MarkFlagRequired("version")

	describeReleaseCmd.Flags().String("deployment-type", "",
		"[Optional] Deployment type of the YugabyteDB version. "+
			"Allowed values: x86_64, aarch64, kubernetes")
}
