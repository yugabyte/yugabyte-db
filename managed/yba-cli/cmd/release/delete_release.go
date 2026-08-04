/*
 * Copyright (c) YugabyteDB, Inc.
 */

package release

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/release/releaseutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteReleaseCmd represents the release command
var deleteReleaseCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB version",
	Long:    "Delete a version in YugabyteDB Anywhere",
	Example: `yba yb-db-version delete --version <version>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		version := releaseutil.VersionValidation(cmd, "delete")
		err := util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", "version", version),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

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
			util.FatalHTTPError(response, err, "Release", "Delete - List Releases")
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

		rDelete, response, err := authAPI.DeleteNewRelease(releaseUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "Delete")
		}

		if rDelete.GetSuccess() {
			logrus.Info(fmt.Sprintf("The version %s has been deleted\n",
				formatter.Colorize(version, formatter.GreenColor)))

		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while deleting version %s\n",
						formatter.Colorize(version, formatter.GreenColor)),
					formatter.RedColor))
		}
	},
}

func init() {
	deleteReleaseCmd.Flags().SortFlags = false
	deleteReleaseCmd.Flags().StringP("version", "v", "",
		"[Required] The YugabyteDB version to be deleted.")
	deleteReleaseCmd.MarkFlagRequired("version")
	deleteReleaseCmd.Flags().String("deployment-type", "",
		"[Optional] Deployment type of the YugabyteDB version. "+
			"Allowed values: x86_64, aarch64, kubernetes")
	deleteReleaseCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
