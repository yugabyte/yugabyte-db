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

// createReleaseCmd represents the ear command
var createReleaseCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB version entry on YugabyteDB Anywhere",
	Long: "Create a YugabyteDB version entry on YugabyteDB Anywhere. " +
		"Run this command after the information provided in the " +
		"\"yba yb-db-version artifact-create <url/upload>\" commands.",
	Example: `yba yb-db-version create --version <version> --type PREVIEW --platform LINUX
	--arch x86_64 --yb-type YBDB --url <url>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		releaseutil.VersionValidation(cmd, "create")
		releaseutil.AddArchValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		releaseutil.IsCommandAllowed(authAPI)

		version, err := cmd.Flags().GetString("version")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		releaseType, err := cmd.Flags().GetString("type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		ybType, err := cmd.Flags().GetString("yb-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		dateMsecs, err := cmd.Flags().GetInt64("date-msecs")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		notes, err := cmd.Flags().GetString("notes")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tag, err := cmd.Flags().GetString("tag")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

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

		artifacts := []ybaclient.Artifact{artifact}
		req := ybaclient.CreateRelease{
			Version:          version,
			ReleaseType:      strings.ToUpper(releaseType),
			Artifacts:        artifacts,
			YbType:           strings.ToUpper(ybType),
			ReleaseDateMsecs: dateMsecs,
			ReleaseNotes:     notes,
			ReleaseTag:       tag,
		}

		rCreate, response, err := authAPI.CreateNewRelease().Release(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "Create")
		}

		resourceUUID := rCreate.GetResourceUUID()
		if util.IsEmptyString(resourceUUID) {
			logrus.Fatalf(
				formatter.Colorize(
					"An error occurred while adding YugabyteDB version.\n",
					formatter.RedColor,
				),
			)
		}

		rGet, response, err := authAPI.GetNewRelease(resourceUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "Create - Get Release")
		}

		r := util.CheckAndAppend(
			make([]ybaclient.ResponseRelease, 0),
			rGet,
			"An error occurred while adding YugabyteDB version",
		)

		releaseCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  release.NewReleaseFormat(viper.GetString("output")),
		}
		release.Write(releaseCtx, r)

	},
}

func init() {
	createReleaseCmd.Flags().SortFlags = false

	createReleaseCmd.Flags().StringP("version", "v", "",
		"[Required] YugabyteDB version to be created")
	createReleaseCmd.MarkFlagRequired("version")

	createReleaseCmd.Flags().String("type", "",
		"[Required] Release type. Allowed values: lts, sts, preview")
	createReleaseCmd.MarkFlagRequired("type")

	createReleaseCmd.Flags().String("platform", "LINUX",
		"[Optional] Platform supported by this version. Allowed values: linux, kubernetes")

	createReleaseCmd.Flags().String("arch", "",
		fmt.Sprintf(
			"[Optional] Architecture supported by this version. %s. "+
				"Allowed values: x86_64, aarch64",
			formatter.Colorize("Required if platform is LINUX", formatter.GreenColor)))

	createReleaseCmd.Flags().String("yb-type", "YBDB",
		"[Optional] Type of the release. Allowed values: YBDB")

	createReleaseCmd.Flags().String("file-id", "",
		fmt.Sprintf("[Optional] File ID of the release tgz file to be used. "+
			"This is the metadata UUID from the \"yba yb-db-version artifact-create upload\" command. %s",
			formatter.Colorize(
				"Provide either file-id or url.",
				formatter.GreenColor)))
	createReleaseCmd.Flags().String("url", "",
		fmt.Sprintf("[Optional] URL to extract release metadata from a remote tarball. %s",
			formatter.Colorize(
				"Provide either file-id or url.",
				formatter.GreenColor)))

	createReleaseCmd.MarkFlagsMutuallyExclusive("file-id", "url")

	createReleaseCmd.Flags().String("sha256", "",
		"[Optional] SHA256 of the release tgz file. Required if file-id is provided.")
	createReleaseCmd.MarkFlagsRequiredTogether("file-id", "sha256")

	createReleaseCmd.Flags().Int64("date-msecs", 0,
		"[Optional] Date in milliseconds since the epoch when the release was created.")

	createReleaseCmd.Flags().String("notes", "",
		"[Optional] Release notes.")

	createReleaseCmd.Flags().String("tag", "", "[Optional] Release tag.")

}
