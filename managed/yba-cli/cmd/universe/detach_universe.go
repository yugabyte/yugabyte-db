/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var detachUniverseCmd = &cobra.Command{
	Use:     "detach",
	Aliases: []string{"export"},
	Short:   "Detach a universe from a YugabyteDB Anywhere",
	Long:    "Detach a universe from a YugabyteDB Anywhere by exporting its metadata and locking it.",
	Example: `yba universe detach --name <universe-name> --path <spec-file-path> --skip-releases=false`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		allowed, version, err := authAPI.NewAttachDetachYBAVersionCheck()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !allowed {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"YugabyteDB Anywhere version %s does not support attach/detach universe commands."+
							" Upgrade to stable version %s.\n",
						version,
						util.YBAAllowNewAttachDetachMinStableVersion,
					),
					formatter.RedColor,
				),
			)
		}

		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		skipReleases, err := cmd.Flags().GetBool("skip-releases")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeListRequest := authAPI.ListUniverses()
		universeListRequest = universeListRequest.Name(universeName)

		rList, responseList, errList := universeListRequest.Execute()
		if errList != nil {
			util.FatalHTTPError(responseList, errList, "Universe", "Detach - List Universes")
		}

		if len(rList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", universeName),
					formatter.RedColor,
				))
		}

		var universeUUID string
		if len(rList) > 0 {
			universeUUID = rList[0].GetUniverseUUID()
		}

		detachUniverseSpec := ybav2client.DetachUniverseSpec{
			SkipReleases: &skipReleases,
		}

		detachRequest := authAPI.DetachUniverse(universeUUID)
		detachRequest = detachRequest.DetachUniverseSpec(detachUniverseSpec)

		_, httpResponse, errDetach := detachRequest.Execute()

		if httpResponse.StatusCode != 200 {
			util.FatalHTTPError(httpResponse, errDetach, "Universe", "Detach")
		}

		// For binary responses, ignore the "undefined response type" error and use httpResponse.Body
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
		fileName := fmt.Sprintf("yb-detached-universe-%s.tar.gz", universeName)
		specFilePath := filepath.Join(directoryPath, fileName)
		_ = perms

		outFile, errCreate := os.Create(specFilePath)
		if errCreate != nil {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"Failed to create output file %s: %s\n",
						specFilePath,
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
						specFilePath,
						errCopy.Error(),
					),
					formatter.RedColor,
				),
			)
		}

		logrus.Infof("The universe %s (%s) has been detached\n",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
	},
}

func init() {
	detachUniverseCmd.Flags().SortFlags = false

	detachUniverseCmd.Flags().
		StringP("name", "n", "", "[Required] Name of the universe to be detached.")
	detachUniverseCmd.MarkFlagRequired("name")
	detachUniverseCmd.Flags().String("path", "",
		"[Optional] The custom directory path to save the detached universe metadata."+
			" If not provided, the metadata will be saved in the path provided in \"directory\".")
	detachUniverseCmd.Flags().
		BoolP("skip-releases", "s", false,
			"[Optional] Whether to skip ybdb and ybc releases from being included in the metadata.")
}
