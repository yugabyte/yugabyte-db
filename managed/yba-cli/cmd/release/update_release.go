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

// updateReleaseCmd represents the ear command
var updateReleaseCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB version entry on YugabyteDB Anywhere",
	Long: "Update a YugabyteDB version entry on YugabyteDB Anywhere. " +
		"Run this command after the information provided in the " +
		"\"yba yb-db-version artifact-create <url/upload>\" commands.",
	Example: `yba yb-db-version update --version <version> --state <state>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		releaseutil.VersionValidation(cmd, "update")
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		version, err := cmd.Flags().GetString("version")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		releasesListRequest := authAPI.ListNewReleases()

		rList, response, err := releasesListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "Create - List Releases")
		}

		requestedReleaseList := make([]ybaclient.ResponseRelease, 0)
		for _, v := range rList {
			if strings.Compare(v.GetVersion(), version) == 0 {
				requestedReleaseList = append(requestedReleaseList, v)
				break
			}
		}

		if len(requestedReleaseList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No YugabyteDB version: %s found\n", version),
					formatter.RedColor,
				))
		}

		requestedRelease := requestedReleaseList[0]

		state, err := cmd.Flags().GetString("state")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(state) {
			if strings.Compare(strings.ToUpper(state), requestedRelease.GetState()) != 0 {
				logrus.Fatal(
					formatter.Colorize(
						fmt.Sprintf(
							"Release state cannot be changed from %s to %s\n",
							requestedRelease.GetState(),
							state,
						),
						formatter.RedColor,
					))
			}
			logrus.Debug("Updating state")
			requestedRelease.SetState(strings.ToUpper(state))
		}

		tag, err := cmd.Flags().GetString("tag")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(tag) {
			logrus.Debug("Updating tag")
			requestedRelease.SetReleaseTag(tag)
		}

		if cmd.Flags().Changed("date-msecs") {
			dateMsecs, err := cmd.Flags().GetInt64("date-msecs")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debug("Updating date-msecs")
			requestedRelease.SetReleaseDateMsecs(dateMsecs)
		}

		notes, err := cmd.Flags().GetString("notes")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(notes) {
			logrus.Debug("Updating notes")
			requestedRelease.SetReleaseNotes(notes)
		}

		req := ybaclient.UpdateRelease{
			State:        requestedRelease.GetState(),
			ReleaseTag:   requestedRelease.GetReleaseTag(),
			ReleaseDate:  requestedRelease.GetReleaseDateMsecs() / 1000,
			ReleaseNotes: requestedRelease.GetReleaseNotes(),
		}

		rUpdate, response, err := authAPI.UpdateNewRelease(
			requestedRelease.GetReleaseUuid()).Release(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "Update")
		}

		if !rUpdate.GetSuccess() {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while updating YugabyteDB version %s (%s)\n",
						formatter.Colorize(version, formatter.GreenColor),
						requestedRelease.GetReleaseUuid()),
					formatter.RedColor))

		}

		rGet, response, err := authAPI.GetNewRelease(requestedRelease.GetReleaseUuid()).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "Update - Get Release")
		}

		r := util.CheckAndAppend(
			make([]ybaclient.ResponseRelease, 0),
			rGet,
			fmt.Sprintf("An error occurred while updating YugabyteDB version %s (%s)",
				version,
				requestedRelease.GetReleaseUuid()),
		)

		releaseCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  release.NewReleaseFormat(viper.GetString("output")),
		}
		release.Write(releaseCtx, r)

	},
}

func init() {
	updateReleaseCmd.Flags().SortFlags = false

	updateReleaseCmd.Flags().StringP("version", "v", "",
		"[Required] YugabyteDB version to be updated.")
	updateReleaseCmd.MarkFlagRequired("version")

	updateReleaseCmd.Flags().String("state", "",
		"[Optional] Update the state of the release. Allowed values: active, disabled")
	updateReleaseCmd.Flags().String("tag", "",
		"[Optional] Update the release tag")

	updateReleaseCmd.Flags().Int64("date-msecs", 0,
		"[Optional] Update Date in milliseconds since the epoch when the release was created.")

	updateReleaseCmd.Flags().String("notes", "",
		"[Optional] Update the release notes.")

}
