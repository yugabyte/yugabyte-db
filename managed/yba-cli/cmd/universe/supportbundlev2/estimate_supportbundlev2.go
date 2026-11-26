/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"encoding/json"
	"net/http"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybav2client "github.com/yugabyte/platform-go-client/v2"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	sbv2formatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/supportbundlev2"
)

// estimateSupportBundleV2Cmd estimates support bundle size via the v2 API.
var estimateSupportBundleV2Cmd = &cobra.Command{
	Use:   "estimate",
	Short: "Estimate support bundle size via the v2 API",
	Long: "Estimate support bundle size for the selected components using the same YAML manifest " +
		"and flags as create. Uses the v2 estimate-size API (synchronous; no bundle is created). " +
		"Pass --output pretty for the nested shape the API returns in a browser.",
	Example: `yba universe support-bundlev2 estimate --name <universe-name>

yba universe support-bundlev2 estimate --components ApplicationLogs

yba universe support-bundlev2 estimate --name <universe-name> \
  --manifest-file devops/bin/support_bundlev2_manifest.yaml \
  --components UniverseLogs --dry-run`,
	PreRun: func(cmd *cobra.Command, args []string) {
		validateSupportBundleV2ManifestFile(cmd, args)
		preflightValidateUniverse(cmd)
	},
	Run: runEstimateSupportBundleV2,
}

// runEstimateSupportBundleV2 executes the support bundle v2 estimate subcommand.
func runEstimateSupportBundleV2(cmd *cobra.Command, args []string) {
	runCtx, err := prepareSupportBundleV2Run(cmd)
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	dryRun, err := cmd.Flags().GetBool("dry-run")
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	if dryRun {
		pretty, _ := json.MarshalIndent(runCtx.requestBody, "", "  ")
		logrus.Infof("DRY-RUN estimate support bundle request body:\n%s\n", string(pretty))
		return
	}

	logrus.Infof(
		"Estimating support bundle size on %s via the estimate-size API\n",
		targetLogLabel(runCtx.ybaOnly, runCtx.targetName, runCtx.targetUUID),
	)

	var estimate *ybav2client.SupportBundleSizeEstimateResponse
	var response *http.Response
	if runCtx.ybaOnly {
		estimate, response, err = runCtx.authAPI.EstimateYbaSupportBundleSizeV2().
			SupportBundleCreateSpec(runCtx.requestBody).Execute()
	} else {
		estimate, response, err = runCtx.authAPI.EstimateSupportBundleSizeV2(runCtx.targetUUID).
			SupportBundleCreateSpec(runCtx.requestBody).Execute()
	}
	if err != nil {
		util.FatalHTTPError(response, err, "Universe: Support Bundle v2", "Estimate Size")
	}

	estimateCtx := formatter.Context{
		Command: "estimate",
		Output:  os.Stdout,
		Format:  sbv2formatter.NewEstimateFormat(viper.GetString("output")),
	}
	if err := sbv2formatter.WriteEstimate(estimateCtx, estimate); err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}
