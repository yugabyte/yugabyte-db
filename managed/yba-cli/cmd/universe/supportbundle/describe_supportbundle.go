/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundle

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/supportbundle"
)

var describeSupportBundleUniverseCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a support bundle for a YugabyteDB Anywhere universe",
	Long:    "Describe a support bundle for a YugabyteDB Anywhere universe",
	Example: `yba universe support-bundle describe --name <universe-name> --uuid <support-bundle-uuid>`,
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
			util.FatalHTTPError(response, err, "Universe: Support Bundle", "Describe")
		}

		supportBundle := util.CheckAndDereference(
			bundle,
			fmt.Sprintf("Support Bundle %s for universe %s (%s) not found",
				uuid,
				universeName,
				universeUUID,
			),
		)

		if supportBundle.GetBundleUUID() == "" {
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

		r := make([]ybaclient.SupportBundle, 0)
		r = append(r, supportBundle)

		supportbundle.Universe = universe

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullSupportBundleContext := *supportbundle.NewFullSupportBundleContext()
			fullSupportBundleContext.Output = os.Stdout
			fullSupportBundleContext.Format = supportbundle.NewFullSupportBundleFormat(
				viper.GetString("output"),
			)
			fullSupportBundleContext.SetFullSupportBundle(r[0])
			fullSupportBundleContext.Write()
			return
		}

		supportBundleCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  supportbundle.NewSupportBundleFormat(viper.GetString("output")),
		}

		supportbundle.Write(supportBundleCtx, r)

	},
}

func init() {
	describeSupportBundleUniverseCmd.Flags().SortFlags = false

	describeSupportBundleUniverseCmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the support bundle to describe.")
	describeSupportBundleUniverseCmd.MarkFlagRequired("uuid")
}
