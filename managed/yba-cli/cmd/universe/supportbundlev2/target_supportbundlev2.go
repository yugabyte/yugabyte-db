/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

type supportBundleV2Target struct {
	authAPI    *ybaAuthClient.AuthAPIClient
	universe   ybaclient.UniverseResp
	ybaOnly    bool
	targetName string
	targetUUID string
}

// preflightValidateUniverse resolves the universe ahead of the command body so that a bad --name
// fails before any confirmation prompt. The result is discarded; every command re-resolves the
// universe when it needs the UUID. --skip-validations skips this check, as it does on the other
// universe commands. A YBA-only bundle has no universe to look up, so it is a no-op there.
func preflightValidateUniverse(cmd *cobra.Command) {
	if !shouldPreflightValidate(cmd) {
		return
	}
	if _, _, err := universeutil.Validations(cmd, util.SupportBundleOperation); err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

// shouldPreflightValidate reports whether the lookup is worth doing: --skip-validations turns it
// off, and a YBA-only bundle has no universe to look up.
func shouldPreflightValidate(cmd *cobra.Command) bool {
	skipValidations, err := cmd.Flags().GetBool("skip-validations")
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if skipValidations {
		return false
	}

	universeName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	return !util.IsEmptyString(universeName)
}

// isYbaOnlyTarget reports the scope, and says so: a forgotten -n would otherwise surface only as an
// empty list or a 404.
func isYbaOnlyTarget(cmd *cobra.Command) (bool, error) {
	universeName, err := cmd.Flags().GetString("name")
	if err != nil {
		return false, err
	}
	if !util.IsEmptyString(universeName) {
		return false, nil
	}
	logrus.Info("No --name given, operating on YBA-only support bundles\n")
	return true, nil
}
func resolveSupportBundleV2Target(cmd *cobra.Command) (*supportBundleV2Target, error) {
	ybaOnly, err := isYbaOnlyTarget(cmd)
	if err != nil {
		return nil, err
	}

	if ybaOnly {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
		return &supportBundleV2Target{
			authAPI:    authAPI,
			ybaOnly:    true,
			targetName: "YBA platform host",
			targetUUID: authAPI.CustomerUUID,
		}, nil
	}

	authAPI, universe, err := universeutil.Validations(cmd, util.SupportBundleOperation)
	if err != nil {
		return nil, err
	}

	return &supportBundleV2Target{
		authAPI:    authAPI,
		universe:   universe,
		ybaOnly:    false,
		targetName: universe.GetName(),
		targetUUID: universe.GetUniverseUUID(),
	}, nil
}

// targetLogLabel names the target in log lines. A universe is identified by its UUID, but a
// YBA-only bundle has none: targetUUID holds the customer UUID there, which identifies neither
// the host nor the bundle and which no YBA-only API call takes. delete, list and download's
// success path already drop it; this is the same rule for the remaining lines.
func targetLogLabel(ybaOnly bool, targetName, targetUUID string) string {
	if ybaOnly {
		return targetName
	}
	return fmt.Sprintf("%s (%s)", targetName, targetUUID)
}
