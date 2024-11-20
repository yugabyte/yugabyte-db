/*
 * Copyright (c) YugaByte, Inc.
 */

package releaseutil

import (
	"fmt"

	"github.com/sirupsen/logrus"
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
