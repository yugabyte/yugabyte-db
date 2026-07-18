/*
 * Copyright (c) YugabyteDB, Inc.
 */

package architecture

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

// editArchitectureReleaseCmd represents the release command to fetch metadata
var editArchitectureReleaseCmd = &cobra.Command{
	Use:   "edit",
	Short: "Edit architectures to a version of YugabyteDB",
	Long: "Edit architectures for a version of YugabyteDB." +
		" Run this command after the information provided in the " +
		"\"yba yb-db-version artifact-create <url/upload>\" commands.",
	Example: `yba yb-db-version architecture edit  --version <version> \
	--platform <platform> --arch <architecture> --sha256 <sha256>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		releaseutil.VersionValidation(cmd, "edit architecture")
		releaseutil.EditArchValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		releaseutil.IsCommandAllowed(authAPI)

		version, err := cmd.Flags().GetString("version")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		releasesListRequest := authAPI.ListNewReleases()

		rList, response, err := releasesListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release Architecture", "Edit - List Releases")
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

		platform, err := cmd.Flags().GetString("platform")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		architecture, err := cmd.Flags().GetString("arch")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		releaseExistingArch := requestedRelease.GetArtifacts()
		var requestedArchitecture ybaclient.Artifact
		requestedArchitectureIndex := -1

		for i, v := range releaseExistingArch {
			if strings.Compare(v.GetPlatform(), "LINUX") == 0 &&
				strings.Compare(v.GetArchitecture(), strings.ToLower(architecture)) == 0 {
				requestedArchitecture = v
				requestedArchitectureIndex = i
				break
			} else if strings.Compare(v.GetPlatform(), "KUBERNETES") == 0 {
				requestedArchitecture = v
				requestedArchitectureIndex = i
				break
			}
		}

		if requestedArchitectureIndex == -1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No architecture: %s of platform: %s found for version: %s\n",
						architecture, platform, version),
					formatter.RedColor,
				))
		}

		sha256, err := cmd.Flags().GetString("sha256")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !util.IsEmptyString(sha256) {
			logrus.Debug("Updating SHA256\n")
			requestedArchitecture.SetSha256(sha256)
			releaseExistingArch[requestedArchitectureIndex] = requestedArchitecture
		}

		req := ybaclient.UpdateRelease{
			State:        requestedRelease.GetState(),
			ReleaseTag:   requestedRelease.GetReleaseTag(),
			ReleaseDate:  requestedRelease.GetReleaseDateMsecs() / 1000,
			ReleaseNotes: requestedRelease.GetReleaseNotes(),
			Artifacts:    releaseExistingArch,
		}

		rUpdate, response, err := authAPI.UpdateNewRelease(
			requestedRelease.GetReleaseUuid()).Release(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release Architecture", "Edit - Update Release")
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
			util.FatalHTTPError(response, err, "Release Architecture", "Edit - Get Release")
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
	editArchitectureReleaseCmd.Flags().SortFlags = false

	editArchitectureReleaseCmd.Flags().String("platform", "",
		"[Required] Platform supported by this version. Allowed values: linux, kubernetes")
	editArchitectureReleaseCmd.MarkFlagRequired("platform")

	editArchitectureReleaseCmd.Flags().String("arch", "",
		fmt.Sprintf(
			"[Optional] Architecture supported by this version. %s. "+
				"Allowed values: x86_64, aarch64",
			formatter.Colorize("Required if platform is LINUX", formatter.GreenColor)))

	editArchitectureReleaseCmd.Flags().String("sha256", "",
		"[Optional] SHA256 of the release tgz file.")

}
