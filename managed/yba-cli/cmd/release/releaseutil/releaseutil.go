/*
 * Copyright (c) YugabyteDB, Inc.
 */

package releaseutil

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// IsCommandAllowed checks if the user is allowed to use the yb-db-version command.
func IsCommandAllowed(authAPI *ybaAuthClient.AuthAPIClient) {
	allowed, version, err := authAPI.NewReleaseYBAVersionCheck()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	if !allowed {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf(
					"YugabyteDB Anywhere version %s does not support yb-db-version commands."+
						" Upgrade to stable version %s.\n",
					version,
					util.YBAAllowNewReleaseMinStableVersion,
				),
				formatter.RedColor,
			),
		)
	}
}

// VersionValidation validates the version flag
func VersionValidation(cmd *cobra.Command, operation string) string {
	version, err := cmd.Flags().GetString("version")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(version) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				fmt.Sprintf("No version found to %s.\n", operation),
				formatter.RedColor))
	}
	return version
}

// AddArchValidation validates the add arch command
func AddArchValidation(cmd *cobra.Command) {
	fileID, err := cmd.Flags().GetString("file-id")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	url, err := cmd.Flags().GetString("url")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(fileID) && util.IsEmptyString(url) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"One of file-id or url needed found to add architecture.\n",
				formatter.RedColor))
	}

	platform, err := cmd.Flags().GetString("platform")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if strings.Compare("LINUX", strings.ToUpper(platform)) == 0 {
		architecture, err := cmd.Flags().GetString("arch")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(architecture) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No architecture found when platform is Linux.\n",
					formatter.RedColor))
		}
	}
}

// EditArchValidation validates the edit arch command
func EditArchValidation(cmd *cobra.Command) {
	platform, err := cmd.Flags().GetString("platform")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if strings.Compare("LINUX", strings.ToUpper(platform)) == 0 {
		architecture, err := cmd.Flags().GetString("arch")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(architecture) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No architecture found when platform is Linux.\n",
					formatter.RedColor))
		}
	}
}
