/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundle

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var downloadSupportBundleUniverseCmd = &cobra.Command{
	Use:     "download",
	Short:   "Download a support bundle for a YugabyteDB Anywhere universe",
	Long:    "Download a support bundle for a YugabyteDB Anywhere universe",
	Example: `yba universe support-bundle download --name <universe-name> --uuid <support-bundle-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		_ = util.MustGetFlagString(cmd, "name")
		_ = util.MustGetFlagString(cmd, "uuid")
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.SupportBundleOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}

		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.SupportBundleOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		bundle, response, err := authAPI.GetSupportBundle(universeUUID, uuid).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe: Support Bundle", "Download - Get Bundle")
		}

		if bundle.GetBundleUUID() == "" {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"No support bundles with uuid: %s found for universe %s (%s)\n",
						uuid,
						universeName,
						universeUUID,
					),
					formatter.RedColor,
				),
			)
		}

		bundlePath := bundle.GetPath()
		if len(bundlePath) == 0 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"Support bundle with uuid: %s is not available to download for universe %s (%s)\n",
						uuid,
						universeName,
						universeUUID,
					),
					formatter.RedColor,
				),
			)
		}
		fileName := filepath.Base(bundlePath)

		rDownload, response, err := authAPI.DownloadSupportBundle(universeUUID, uuid).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe: Support Bundle", "Download")
		}

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

		if len(rDownload) > 0 {
			filePath := filepath.Join(
				directoryPath,
				fileName,
			)
			err = os.WriteFile(filePath, []byte(rDownload), perms)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(
						"Error writing support bundle: "+err.Error()+"\n",
						formatter.RedColor,
					),
				)
			}

			logrus.Info(
				fmt.Sprintf(
					"The support bundle %s from universe %s (%s) has been downloaded to %s\n",
					formatter.Colorize(uuid, formatter.GreenColor),
					universeName,
					universeUUID,
					formatter.Colorize(filePath, formatter.GreenColor),
				),
			)

		}

	},
}

func init() {
	downloadSupportBundleUniverseCmd.Flags().SortFlags = false

	downloadSupportBundleUniverseCmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the support bundle to download.")
	downloadSupportBundleUniverseCmd.MarkFlagRequired("uuid")
	downloadSupportBundleUniverseCmd.Flags().String("path", "",
		"[Optional] The custom directory path to save the downloaded support bundle."+
			" If not provided, the bundle will be saved in the path provided in \"directory\".")
}
