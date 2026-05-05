/*
 * Copyright (c) YugabyteDB, Inc.
 */

package filecollection

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybav2client "github.com/yugabyte/platform-go-client/v2"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	fcformatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/filecollection"
)

var createFileCollectionCmd = &cobra.Command{
	Use:   "create",
	Short: "Collect files from database nodes in a universe",
	Long: "Collect specified files from one or more database nodes in a universe. " +
		"Files are packaged into a tar.gz archive on each node.",
	Example: `yba universe file-collection create --name <universe-name> \
  --file-paths "/home/yugabyte/tserver/logs/yb-tserver.INFO,/home/yugabyte/master/logs/yb-master.INFO"

yba universe file-collection create --name <universe-name> \
  --file-paths "/home/yugabyte/bin/version_metadata.json" \
  --directory-paths "/home/yugabyte/tserver/logs" --max-depth 2

yba universe file-collection create --name <universe-name> \
  --file-paths "/home/yugabyte/tserver/logs/yb-tserver.INFO" \
  --node-names "yb-1-node-n1,yb-1-node-n2" --masters-only`,
	PreRun: func(cmd *cobra.Command, args []string) {
		_ = util.MustGetFlagString(cmd, "name")

		filePaths, err := cmd.Flags().GetString("file-paths")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		dirPaths, err := cmd.Flags().GetString("directory-paths")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(filePaths) && util.IsEmptyString(dirPaths) {
			logrus.Fatalf(
				formatter.Colorize(
					"At least one of --file-paths or --directory-paths is required\n",
					formatter.RedColor,
				),
			)
		}
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

		filePathsStr, err := cmd.Flags().GetString("file-paths")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var filePaths []string
		if !util.IsEmptyString(filePathsStr) {
			for _, p := range strings.Split(filePathsStr, ",") {
				p = strings.TrimSpace(p)
				if !util.IsEmptyString(p) {
					filePaths = append(filePaths, p)
				}
			}
		}

		dirPathsStr, err := cmd.Flags().GetString("directory-paths")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var dirPaths []string
		if !util.IsEmptyString(dirPathsStr) {
			for _, p := range strings.Split(dirPathsStr, ",") {
				p = strings.TrimSpace(p)
				if !util.IsEmptyString(p) {
					dirPaths = append(dirPaths, p)
				}
			}
		}

		options := *ybav2client.NewFileCollectionOptions()

		if len(dirPaths) > 0 {
			options.SetDirectoryPaths(dirPaths)
		}

		maxDepth, err := cmd.Flags().GetInt32("max-depth")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if cmd.Flags().Changed("max-depth") {
			options.SetMaxDepth(maxDepth)
		}

		maxFileSize, err := cmd.Flags().GetInt64("max-file-size-bytes")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if cmd.Flags().Changed("max-file-size-bytes") {
			options.SetMaxFileSizeBytes(maxFileSize)
		}

		maxTotalSize, err := cmd.Flags().GetInt64("max-total-size-bytes")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if cmd.Flags().Changed("max-total-size-bytes") {
			options.SetMaxTotalSizeBytes(maxTotalSize)
		}

		timeoutSecs, err := cmd.Flags().GetInt64("timeout-secs")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if cmd.Flags().Changed("timeout-secs") {
			options.SetTimeoutSecs(timeoutSecs)
		}

		linuxUser, err := cmd.Flags().GetString("linux-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if linuxUser != "" {
			options.SetLinuxUser(linuxUser)
		}

		requestBody := *ybav2client.NewCollectFilesRequest(options)

		nodeNamesStr, err := cmd.Flags().GetString("node-names")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		mastersOnly, err := cmd.Flags().GetBool("masters-only")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		tserversOnly, err := cmd.Flags().GetBool("tservers-only")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		hasNodeSelection := !util.IsEmptyString(nodeNamesStr) || mastersOnly || tserversOnly
		if hasNodeSelection {
			nodeSelection := ybav2client.NewNodeSelection()
			if !util.IsEmptyString(nodeNamesStr) {
				var nodeNames []string
				for _, n := range strings.Split(nodeNamesStr, ",") {
					if util.IsEmptyString(n) {
						continue
					}
					nodeNames = append(nodeNames, strings.TrimSpace(n))
				}
				nodeSelection.SetNodeNames(nodeNames)
			}
			if mastersOnly {
				nodeSelection.SetMastersOnly(true)
			}
			if tserversOnly {
				nodeSelection.SetTserversOnly(true)
			}
			requestBody.SetNodes(*nodeSelection)
		}

		resp, httpResponse, err := authAPI.CreateFileCollection(universeUUID).
			CollectFilesRequest(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(httpResponse, err, "Universe: File Collection", "Create")
		}

		summary := resp.GetSummary()
		collectionUUID := summary.GetCollectionUuid()

		ctx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  fcformatter.NewSummaryFormat(viper.GetString("output")),
		}

		if util.IsEmptyString(collectionUUID) {
			if !(ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) {
				logrus.Warnf(
					formatter.Colorize(
						fmt.Sprintf(
							"No files were collected from universe %s (%s); "+
								"no collection was persisted and nothing is available to download. "+
								"use --output json for full details, "+
								"including skip reasons and error messages.\n",
							universeName,
							universeUUID,
						),
						formatter.YellowColor,
					),
				)
				fcformatter.WriteSummary(ctx, resp)

				totalFailed := summary.GetTotalFilesFailed()
				totalSkipped := summary.GetTotalFilesSkipped()
				logrus.Fatalf(
					formatter.Colorize(
						fmt.Sprintf(
							"File collection produced no downloadable archive "+
								"(files failed: %d, files skipped: %d). "+
								"Verify --file-paths exist on the nodes and raise "+
								"--max-file-size-bytes / --max-total-size-bytes as needed.\n",
							totalFailed,
							totalSkipped,
						),
						formatter.RedColor,
					),
				)
			}
		}

		logrus.Info(
			fmt.Sprintf(
				"File collection %s completed for universe %s (%s)\n",
				formatter.Colorize(collectionUUID, formatter.GreenColor),
				universeName,
				universeUUID,
			),
		)

		fcformatter.WriteSummary(ctx, resp)
	},
}

func init() {
	createFileCollectionCmd.Flags().SortFlags = false

	createFileCollectionCmd.Flags().String("file-paths", "",
		fmt.Sprintf("[Optional*] Comma-separated list of file paths to collect from each node. "+
			"Paths can be absolute or relative to the yugabyte home directory. %s",
			formatter.Colorize(
				"At least one of file-paths or directory-paths is required.",
				formatter.YellowColor,
			)))
	createFileCollectionCmd.Flags().String("directory-paths", "",
		fmt.Sprintf("[Optional*] Comma-separated list of directory paths to collect files from. %s",
			formatter.Colorize(
				"At least one of file-paths or directory-paths is required.",
				formatter.YellowColor,
			)))

	createFileCollectionCmd.Flags().Int32("max-depth", 1,
		"[Optional] Maximum depth for directory traversal (1-10).")
	createFileCollectionCmd.Flags().Int64("max-file-size-bytes", 10485760,
		"[Optional] Maximum size of individual files to collect in bytes (default 10MB).")
	createFileCollectionCmd.Flags().Int64("max-total-size-bytes", 104857600,
		"[Optional] Maximum total size of all files per node in bytes (default 100MB).")
	createFileCollectionCmd.Flags().Int64("timeout-secs", 300,
		"[Optional] Timeout in seconds for file collection on each node (default 300).")
	createFileCollectionCmd.Flags().String("linux-user", "yugabyte",
		"[Optional] Linux user to run file collection as.")

	createFileCollectionCmd.Flags().String("node-names", "",
		"[Optional] Comma-separated list of specific node names to collect from.")
	createFileCollectionCmd.Flags().Bool("masters-only", false,
		"[Optional] Collect files only from master nodes. (default false)")
	createFileCollectionCmd.Flags().Bool("tservers-only", false,
		"[Optional] Collect files only from tserver nodes. (default false)")
}
