/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"
	"io"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var downloadSupportBundleV2Cmd = &cobra.Command{
	Use:   "download",
	Short: "Download a support bundle created via the v2 API",
	Long:  "Download a support bundle for a universe or a YBA-only bundle from the platform host.",
	Example: `yba universe support-bundlev2 download --name <universe-name> --uuid <support-bundle-uuid>

yba universe support-bundlev2 download --uuid <support-bundle-uuid> --path /tmp`,
	PreRun: func(cmd *cobra.Command, args []string) {
		_ = util.MustGetFlagString(cmd, "uuid")
		preflightValidateUniverse(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		target, err := resolveSupportBundleV2Target(cmd)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		bundleUUID := util.MustGetFlagString(cmd, "uuid")

		bundle, response, err := getSupportBundleV2(target, bundleUUID)
		if err != nil {
			util.FatalHTTPError(response, err, "Universe: Support Bundle v2", "Get")
		}
		if bundle.GetBundleUUID() == "" {
			logrus.Fatal(formatter.Colorize(
				fmt.Sprintf("No support bundle with uuid %s found\n", bundleUUID),
				formatter.RedColor,
			))
		}
		if !strings.EqualFold(bundle.GetStatus(), util.SuccessSupportBundleState) {
			logrus.Fatal(formatter.Colorize(
				fmt.Sprintf(
					"Support bundle %s is in %s state and is not available to download on %s\n",
					bundleUUID,
					bundle.GetStatus(),
					targetLogLabel(target.ybaOnly, target.targetName, target.targetUUID),
				),
				formatter.RedColor,
			))
		}

		// The download is marked x-yba-api-stream-response, so the archive is still in the
		// response body here rather than buffered by the client: copy it straight to disk.
		httpResponse, err := downloadSupportBundleV2(target, bundleUUID)
		if err != nil {
			util.FatalHTTPError(httpResponse, err, "Universe: Support Bundle v2", "Download")
		}
		defer httpResponse.Body.Close()

		if httpResponse.StatusCode < 200 || httpResponse.StatusCode >= 300 {
			util.FatalHTTPError(
				httpResponse,
				fmt.Errorf("HTTP %d", httpResponse.StatusCode),
				"Universe: Support Bundle v2",
				"Download",
			)
		}

		path, err := cmd.Flags().GetString("path")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		directoryPath := ""
		if len(path) > 0 {
			directoryPath = path
		} else {
			directoryPath, _, err = util.GetCLIConfigDirectoryPath()
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		if len(directoryPath) == 0 {
			directoryPath = "."
		}

		fileName := supportBundleV2DownloadFileName(httpResponse, bundleUUID)

		filePath, err := writeBundleToFile(httpResponse.Body, directoryPath, fileName)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if target.ybaOnly {
			logrus.Info(fmt.Sprintf(
				"The YBA-only support bundle %s has been downloaded to %s\n",
				formatter.Colorize(bundleUUID, formatter.GreenColor),
				formatter.Colorize(filePath, formatter.GreenColor),
			))
			return
		}

		logrus.Info(fmt.Sprintf(
			"The support bundle %s from universe %s (%s) has been downloaded to %s\n",
			formatter.Colorize(bundleUUID, formatter.GreenColor),
			target.targetName,
			target.targetUUID,
			formatter.Colorize(filePath, formatter.GreenColor),
		))
	},
}

func getSupportBundleV2(
	target *supportBundleV2Target,
	bundleUUID string,
) (ybaclient.SupportBundle, *http.Response, error) {
	var bundle *ybav2client.SupportBundle
	var response *http.Response
	var err error
	if target.ybaOnly {
		bundle, response, err = target.authAPI.GetYbaSupportBundleV2(bundleUUID).Execute()
	} else {
		bundle, response, err = target.authAPI.GetSupportBundleV2(
			target.targetUUID, bundleUUID).Execute()
	}
	if err != nil {
		return ybaclient.SupportBundle{}, response, err
	}
	if bundle == nil {
		return ybaclient.SupportBundle{}, response, nil
	}
	return supportBundleV2ToClient(*bundle), response, nil
}
func downloadSupportBundleV2(
	target *supportBundleV2Target,
	bundleUUID string,
) (*http.Response, error) {
	if target.ybaOnly {
		return target.authAPI.DownloadYbaSupportBundleV2(bundleUUID).Execute()
	}
	return target.authAPI.DownloadSupportBundleV2(target.targetUUID, bundleUUID).Execute()
}

// supportBundleV2DownloadFileName names the downloaded archive. The v2 API no longer exposes the
// server-side path, so the name comes from the Content-Disposition header the download sets.
func supportBundleV2DownloadFileName(resp *http.Response, bundleUUID string) string {
	if resp != nil {
		if _, params, err := mime.ParseMediaType(
			resp.Header.Get("Content-Disposition"),
		); err == nil {
			// filepath.Base strips every directory component, so a traversal in the header
			// lands inside the download directory rather than escaping it. What it can still
			// return is ".", ".." or the separator, none of which is a usable file name: ".."
			// would resolve to the parent directory and fail as "is a directory".
			if fileName := filepath.Base(strings.TrimSpace(params["filename"])); fileName != "" &&
				fileName != "." && fileName != ".." &&
				fileName != string(filepath.Separator) {
				return fileName
			}
		}
	}
	return fmt.Sprintf("support-bundle-%s.tar.gz", bundleUUID)
}

// bundleFileMode is what os.Create produced before the archive was staged through a temp file:
// 0644 after the usual umask, and what the v1 download writes.
const bundleFileMode = 0644

// writeBundleToFile streams body into directoryPath/fileName and returns the path written. The
// archive is staged in a temp file in the same directory and renamed once whole, so a failed
// download neither destroys an existing bundle nor leaves a fragment under the final name. Cleanup
// is explicit at each exit, not deferred: logrus.Fatal exits without running deferred functions.
func writeBundleToFile(body io.Reader, directoryPath, fileName string) (string, error) {
	filePath := filepath.Join(directoryPath, fileName)

	tmpFile, err := os.CreateTemp(directoryPath, fileName+".part-*")
	if err != nil {
		return "", fmt.Errorf("Failed to create a temporary file in %s: %w", directoryPath, err)
	}
	tmpPath := tmpFile.Name()

	if _, err := io.Copy(tmpFile, body); err != nil {
		tmpFile.Close()
		os.Remove(tmpPath)
		return "", fmt.Errorf("Failed to write support bundle to %s: %w", filePath, err)
	}

	// Checked rather than deferred: a full disk and an NFS write-back failure both surface here
	// and not from io.Copy, so discarding this would report a bundle that never landed.
	if err := tmpFile.Close(); err != nil {
		os.Remove(tmpPath)
		return "", fmt.Errorf("Failed to write support bundle to %s: %w", filePath, err)
	}

	if err := os.Chmod(tmpPath, bundleFileMode); err != nil {
		os.Remove(tmpPath)
		return "", fmt.Errorf("Failed to set permissions on %s: %w", filePath, err)
	}

	if err := os.Rename(tmpPath, filePath); err != nil {
		os.Remove(tmpPath)
		return "", fmt.Errorf("Failed to move the support bundle into %s: %w", filePath, err)
	}

	return filePath, nil
}
