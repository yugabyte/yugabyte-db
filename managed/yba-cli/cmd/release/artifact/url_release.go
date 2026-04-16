/*
 * Copyright (c) YugabyteDB, Inc.
 */

package artifact

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/release/releaseutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/release/extractmetadata"
)

var urlArtifactReleaseCmd = &cobra.Command{
	Use:   "url",
	Short: "Fetch metadata of a new YugabyteDB version from a URL",
	Long: "Fetch metadata of a new version of YugabyteDB from a URL. " +
		"Use the output of this command in the \"yba yb-db-version create\" command.",
	Example: `yba yb-db-version artifact-create url --url <url>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		url, err := cmd.Flags().GetString("url")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(url) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No URL found to fetch metadata.\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		releaseutil.IsCommandAllowed(authAPI)

		url, err := cmd.Flags().GetString("url")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.ExtractMetadata{
			Url: url,
		}

		rRelease, response, err := authAPI.ExtractMetadata().ReleaseURL(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Release", "URL")
		}

		resourceUUID := rRelease.GetResourceUUID()
		if util.IsEmptyString(resourceUUID) {
			logrus.Fatalf(
				formatter.Colorize(
					"An error occurred while extracting metadata from URL.\n",
					formatter.RedColor,
				),
			)
		}

		logrus.Infof("Resource UUID for current release: %s\n", resourceUUID)
		logrus.Debug("Extracting Metadata...\n")

		msg := fmt.Sprintf("The resource %s extraction is in progress",
			formatter.Colorize(resourceUUID, formatter.GreenColor))

		r, err := authAPI.WaitForExtractMetadata(resourceUUID, msg, "url")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rList := make([]ybaclient.ResponseExtractMetadata, 0)

		rList = append(rList, r)

		if len(rList) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullExtractMetadataContext := *extractmetadata.NewFullExtractMetadataContext()
			fullExtractMetadataContext.Output = os.Stdout
			fullExtractMetadataContext.Format =
				extractmetadata.NewFullExtractMetadataFormat(viper.GetString("output"))
			fullExtractMetadataContext.SetFullExtractMetadata(r)
			fullExtractMetadataContext.Write()
			return
		}

		extractmetadatasCtx := formatter.Context{
			Command: "artifact",
			Output:  os.Stdout,
			Format:  extractmetadata.NewExtractMetadataFormat(viper.GetString("output")),
		}

		extractmetadata.Write(extractmetadatasCtx, rList)

	},
}

func init() {
	urlArtifactReleaseCmd.Flags().SortFlags = false

	urlArtifactReleaseCmd.Flags().String("url", "", "[Required] URL of the release")
	urlArtifactReleaseCmd.MarkFlagRequired("url")
}
