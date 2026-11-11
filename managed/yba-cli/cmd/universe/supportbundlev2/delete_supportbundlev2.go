/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"
	"net/http"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var deleteSupportBundleV2Cmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"rm", "remove"},
	Short:   "Delete a support bundle created via the v2 API",
	Long:    "Delete a support bundle for a universe or a YBA-only bundle from the platform host.",
	Example: `yba universe support-bundlev2 delete --name <universe-name> --uuid <support-bundle-uuid>

yba universe support-bundlev2 delete --uuid <support-bundle-uuid> --force`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		bundleUUID := util.MustGetFlagString(cmd, "uuid")
		preflightValidateUniverse(cmd)
		universeName, _ := cmd.Flags().GetString("name")
		targetLabel := "YBA platform host"
		if !util.IsEmptyString(universeName) {
			targetLabel = fmt.Sprintf("universe %s", universeName)
		}
		if err := util.ConfirmCommand(
			fmt.Sprintf(
				"Are you sure you want to delete support bundle %s from %s",
				bundleUUID,
				targetLabel,
			),
			viper.GetBool("force"),
		); err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		target, err := resolveSupportBundleV2Target(cmd)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		bundleUUID := util.MustGetFlagString(cmd, "uuid")
		response, err := deleteSupportBundleV2(target, bundleUUID)
		if err != nil {
			util.FatalHTTPError(response, err, "Universe: Support Bundle v2", "Delete")
		}

		if target.ybaOnly {
			logrus.Info(fmt.Sprintf(
				"The YBA-only support bundle %s has been deleted\n",
				formatter.Colorize(bundleUUID, formatter.GreenColor),
			))
			return
		}

		logrus.Info(fmt.Sprintf(
			"The support bundle %s from universe %s (%s) has been deleted\n",
			formatter.Colorize(bundleUUID, formatter.GreenColor),
			target.targetName,
			target.targetUUID,
		))
	},
}

func deleteSupportBundleV2(
	target *supportBundleV2Target,
	bundleUUID string,
) (*http.Response, error) {
	if target.ybaOnly {
		return target.authAPI.DeleteYbaSupportBundleV2(bundleUUID).Execute()
	}
	return target.authAPI.DeleteSupportBundleV2(target.targetUUID, bundleUUID).Execute()
}
