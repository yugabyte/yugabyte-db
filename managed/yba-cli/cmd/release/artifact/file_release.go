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

var fileArtifactReleaseCmd = &cobra.Command{
	Use:   "upload",
	Short: "Upload a new YugabyteDB version from a tar gz file",
	Long: "Upload a new version of YugabyteDB from a tar gz file. " +
		"Use the output of this command in the \"yba yb-db-version create\" command.",
	Example: `yba yb-db-version artifact-create upload --file <file>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		file, err := cmd.Flags().GetString("file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(file) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No tar gz file found to upload.\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		releaseutil.IsCommandAllowed(authAPI)

		filePath, err := cmd.Flags().GetString("file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rRelease, err := authAPI.UploadReleaseRest(filePath)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		resourceUUID := rRelease.GetResourceUUID()
		if util.IsEmptyString(resourceUUID) {
			logrus.Fatalf(
				formatter.Colorize(
					"An error occurred while extracting metadata from tar gz file.\n",
					formatter.RedColor,
				),
			)
		}

		logrus.Debugf("Resource UUID for current release: %s\n", resourceUUID)
		logrus.Debug("Extracting Metadata...\n")

		msg := fmt.Sprintf("The resource %s extraction is in progress",
			formatter.Colorize(resourceUUID, formatter.GreenColor))

		r, err := authAPI.WaitForExtractMetadata(resourceUUID, msg, "file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		r.SetMetadataUuid(resourceUUID)
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
			Command: "upload",
			Output:  os.Stdout,
			Format:  extractmetadata.NewExtractMetadataFormat(viper.GetString("output")),
		}

		extractmetadata.Write(extractmetadatasCtx, rList)

	},
}

func init() {
	fileArtifactReleaseCmd.Flags().SortFlags = false

	fileArtifactReleaseCmd.Flags().String("file", "", "[Required] Tar gz file path of the release")
	fileArtifactReleaseCmd.MarkFlagRequired("file")
}
