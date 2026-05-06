/*
 * Copyright (c) YugabyteDB, Inc.
 */

package filecollection

import (
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var downloadFileCollectionCmd = &cobra.Command{
	Use:   "download",
	Short: "Download collected files from database nodes",
	Long: "Download previously collected files from database nodes. " +
		"Use the collection UUID returned by the create command.",
	Example: `yba universe file-collection download --name <universe-name> --uuid <collection-uuid>

yba universe file-collection download --name <universe-name> --uuid <collection-uuid> \
  --path /tmp/diagnostics --cleanup-db-nodes-after`,
	PreRun: func(cmd *cobra.Command, args []string) {
		_ = util.MustGetFlagString(cmd, "name")
		_ = util.MustGetFlagString(cmd, "uuid")
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.FileCollectionOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.FileCollectionOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		collectionUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		cleanupAfter, err := cmd.Flags().GetBool("cleanup-db-nodes-after")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		downloadRequest := authAPI.DownloadFileCollection(universeUUID, collectionUUID)
		if cleanupAfter {
			downloadRequest = downloadRequest.CleanupDbNodesAfter(true)
		}

		_, httpResponse, err := downloadRequest.Execute()

		if err != nil {
			util.FatalHTTPError(httpResponse, err, "Universe: File Collection", "Download")
		}

		contentType := httpResponse.Header.Get("Content-Type")
		if contentType != "application/gzip" && contentType != "application/octet-stream" {
			logrus.Fatalf(
				formatter.Colorize(
					"Expected gzip response but got: "+contentType+"\n",
					formatter.RedColor,
				),
			)
		}

		defer httpResponse.Body.Close()

		path, err := cmd.Flags().GetString("path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		directoryPath := ""
		perms := fs.FileMode(0644)
		if len(path) > 0 {
			perms = util.GetDirectoryPermissions(path)
			directoryPath = path
		} else {
			directoryPath, perms, err = util.GetCLIConfigDirectoryPath()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		if len(directoryPath) == 0 {
			directoryPath = "."
			perms = 0644
		}
		_ = perms

		fileName := fmt.Sprintf("file-collection-%s.tar.gz", collectionUUID)
		filePath := filepath.Join(directoryPath, fileName)

		outFile, errCreate := os.Create(filePath)
		if errCreate != nil {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"Failed to create output file %s: %s\n",
						filePath,
						errCreate.Error(),
					),
					formatter.RedColor,
				),
			)
		}
		defer outFile.Close()

		_, errCopy := io.Copy(outFile, httpResponse.Body)
		if errCopy != nil {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"Failed to write to output file %s: %s\n",
						filePath,
						errCopy.Error(),
					),
					formatter.RedColor,
				),
			)
		}

		logrus.Info(
			fmt.Sprintf(
				"File collection %s from universe %s (%s) has been downloaded to %s\n",
				formatter.Colorize(collectionUUID, formatter.GreenColor),
				universeName,
				universeUUID,
				formatter.Colorize(filePath, formatter.GreenColor),
			),
		)
	},
}

func init() {
	downloadFileCollectionCmd.Flags().SortFlags = false

	downloadFileCollectionCmd.Flags().StringP("uuid", "u", "",
		"[Required] The collection UUID returned by the create command.")
	downloadFileCollectionCmd.MarkFlagRequired("uuid")
	downloadFileCollectionCmd.Flags().String("path", "",
		"[Optional] Custom directory path to save the downloaded archive. "+
			"If not provided, saves to the CLI config directory.")
	downloadFileCollectionCmd.Flags().Bool("cleanup-db-nodes-after", false,
		"[Optional] Automatically delete collected files from DB nodes after download.")
}
