/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"archive/tar"
	"compress/gzip"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// extractUniverseUUIDFromSpec extracts the universe UUID from the tar.gz spec file
func extractUniverseUUIDFromSpec(tarGzPath string) (string, error) {
	file, err := os.Open(tarGzPath)
	if err != nil {
		return "", fmt.Errorf("failed to open tar.gz file: %v", err)
	}
	defer file.Close()

	gzReader, err := gzip.NewReader(file)
	if err != nil {
		return "", fmt.Errorf("failed to create gzip reader: %v", err)
	}
	defer gzReader.Close()

	tarReader := tar.NewReader(gzReader)

	for {
		header, err := tarReader.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return "", fmt.Errorf("failed to read tar entry: %v", err)
		}

		if strings.HasSuffix(header.Name, "attach-detach-spec.json") {
			var jsonData map[string]interface{}
			decoder := json.NewDecoder(tarReader)
			if err := decoder.Decode(&jsonData); err != nil {
				return "", fmt.Errorf("failed to decode attach-detach-spec.json: %v", err)
			}

			// Extract universe.universeUUID
			if universe, ok := jsonData["universe"].(map[string]interface{}); ok {
				if universeUUID, ok := universe["universeUUID"].(string); ok && universeUUID != "" {
					return universeUUID, nil
				}
			}

			return "", fmt.Errorf("universeUUID not found in attach-detach-spec.json")
		}
	}

	return "", fmt.Errorf("attach-detach-spec.json not found in tar.gz file")
}

var attachUniverseCmd = &cobra.Command{
	Use:   "attach",
	Short: "Attach universe to a destination YugabyteDB Anywhere",
	Long: "Attach a previously detached universe to this YugabyteDB Anywhere " +
		"using its metadata spec (tar.gz) file.",
	Example: `yba universe attach --name <universe-name> --path <spec-file-path>`,
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

		universeListRequest := authAPI.ListUniverses()
		universeListRequest = universeListRequest.Name(universeName)

		rList, responseList, errList := universeListRequest.Execute()
		if errList != nil {
			util.FatalHTTPError(
				responseList,
				errList,
				"Universe",
				"Attach - Check Existing Universe",
			)
		}

		if len(rList) > 0 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"Universe with name '%s' already exists. Cannot attach universe with duplicate name.\n",
						universeName,
					),
					formatter.RedColor,
				))
		}

		path, err := cmd.Flags().GetString("path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		directoryPath := ""
		var specFilePath string

		if len(path) > 0 {
			// Check if the provided path is a directory or a file
			if stat, err := os.Stat(path); err == nil && stat.IsDir() {
				directoryPath = path
				fileName := fmt.Sprintf("yb-detached-universe-%s.tar.gz", universeName)
				specFilePath = filepath.Join(directoryPath, fileName)
			} else {
				specFilePath = path
			}
		} else {
			directoryPath, _, err = util.GetCLIConfigDirectoryPath()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(directoryPath) == 0 {
				directoryPath = "."
			}
			fileName := fmt.Sprintf("yb-detached-universe-%s.tar.gz", universeName)
			specFilePath = filepath.Join(directoryPath, fileName)
		}

		if _, err := os.Stat(specFilePath); os.IsNotExist(err) {
			logrus.Fatalf(formatter.Colorize(
				fmt.Sprintf("Specified spec file does not exist: %s\n", specFilePath),
				formatter.RedColor,
			))
		}

		sourceUniverseUUID, err := extractUniverseUUIDFromSpec(specFilePath)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(
				fmt.Sprintf("Failed to extract universe UUID from spec file: %s\n", err.Error()),
				formatter.RedColor,
			))
		}

		// Use the REST API from the internal client
		err = authAPI.AttachUniverseRest(sourceUniverseUUID, specFilePath)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		logrus.Infof("The universe %s (%s) has been attached\n",
			formatter.Colorize(universeName, formatter.GreenColor), sourceUniverseUUID)
	},
}

func init() {
	attachUniverseCmd.Flags().SortFlags = false

	attachUniverseCmd.Flags().
		StringP("name", "n", "", "[Required] Name of the universe to be attached.")
	attachUniverseCmd.MarkFlagRequired("name")

	attachUniverseCmd.Flags().String("path", "",
		"[Optional] The custom directory path or file path to the universe metadata file. "+
			"If not provided, the CLI will look for 'yb-detached-universe-<universe-name>.tar.gz' "+
			"in the directory specified by --directory.")
}
