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

// addArchitectureReleaseCmd represents the release command to fetch metadata
var addArchitectureReleaseCmd = &cobra.Command{
	Use:   "add",
	Short: "Add architectures to a version of YugabyteDB",
	Long: "Add architectures for a version of YugabyteDB." +
		" Run this command after the information provided in the " +
		"\"yba yb-db-version artifact-create <url/upload>\" commands.",
	Example: `yba yb-db-version architecture add  --version <version> \
	--platform <platform> --arch <architecture> --url <url>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		releaseutil.VersionValidation(cmd, "add architecture")
		releaseutil.AddArchValidation(cmd)
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
			util.FatalHTTPError(response, err, "Release Architecture", "Add - List Releases")
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

		fileID, err := cmd.Flags().GetString("file-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var sha256 string

		if !util.IsEmptyString(fileID) {
			sha256, err = cmd.Flags().GetString("sha256")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		url, err := cmd.Flags().GetString("url")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		artifact := ybaclient.Artifact{
			Platform:      strings.ToUpper(platform),
			Architecture:  strings.ToLower(architecture),
			PackageFileId: fileID,
			Sha256:        sha256,
			PackageUrl:    url,
		}

		addArtifact := requestedRelease.GetArtifacts()
		addArtifact = append(addArtifact, artifact)
		requestedRelease.SetArtifacts(addArtifact)

		req := ybaclient.UpdateRelease{
			State:        requestedRelease.GetState(),
			ReleaseTag:   requestedRelease.GetReleaseTag(),
			ReleaseDate:  requestedRelease.GetReleaseDateMsecs() / 1000,
			ReleaseNotes: requestedRelease.GetReleaseNotes(),
			Artifacts:    addArtifact,
		}

		rUpdate, response, err := authAPI.UpdateNewRelease(
			requestedRelease.GetReleaseUuid()).Release(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release Architecture", "Add - Update Release")
		}

		updateResult := util.CheckAndDereference(
			rUpdate,
			fmt.Sprintf("An error occurred while updating YugabyteDB version %s (%s)",
				version,
				requestedRelease.GetReleaseUuid()),
		)

		if !updateResult.GetSuccess() {
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
			util.FatalHTTPError(response, err, "Release Architecture", "Add - Get Release")
		}

		r := make([]ybaclient.ResponseRelease, 0)
		r = append(r, *rGet)

		releaseCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  release.NewReleaseFormat(viper.GetString("output")),
		}
		release.Write(releaseCtx, r)
	},
}

func init() {
	addArchitectureReleaseCmd.Flags().SortFlags = false

	addArchitectureReleaseCmd.Flags().String("platform", "LINUX",
		"[Optional] Platform supported by this version. Allowed values: linux, kubernetes")

	addArchitectureReleaseCmd.Flags().String("arch", "",
		fmt.Sprintf(
			"[Optional] Architecture supported by this version. %s. "+
				"Allowed values: x86_64, aarch64",
			formatter.Colorize("Required if platform is LINUX", formatter.GreenColor)))

	addArchitectureReleaseCmd.Flags().String("file-id", "",
		fmt.Sprintf("[Optional] File ID of the release tgz file to be used. "+
			"This is the metadata UUID from the \"yba yb-db-version artifact-create upload\" command. %s",
			formatter.Colorize(
				"Provide either file-id or url.",
				formatter.GreenColor)))
	addArchitectureReleaseCmd.Flags().String("url", "",
		fmt.Sprintf("[Optional] URL to extract release metadata from a remote tarball. %s",
			formatter.Colorize(
				"Provide either file-id or url.",
				formatter.GreenColor)))

	addArchitectureReleaseCmd.MarkFlagsMutuallyExclusive("file-id", "url")

	addArchitectureReleaseCmd.Flags().String("sha256", "",
		"[Optional] SHA256 of the release tgz file. Required if file-id is provided.")
	addArchitectureReleaseCmd.MarkFlagsRequiredTogether("file-id", "sha256")

}
