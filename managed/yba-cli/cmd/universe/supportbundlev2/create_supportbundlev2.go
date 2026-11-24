/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/supportbundle"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// createSupportBundleV2Cmd creates a support bundle via the v2 API.
var createSupportBundleV2Cmd = &cobra.Command{
	Use:   "create",
	Short: "Create a support bundle via the v2 API",
	Long:  "Collect support-bundle components using a YAML config consumed by this CLI.",
	Example: `yba universe support-bundlev2 create --name <universe-name>

yba universe support-bundlev2 create --components ApplicationLogs --force

yba universe support-bundlev2 create --name <universe-name> \
  --node-names yb-15-universe-n1,yb-15-universe-n2

yba universe support-bundlev2 create --name <universe-name> \
  --manifest-file devops/bin/support_bundlev2_manifest.yaml \
  --components UniverseLogs,ApplicationLogs --dry-run`,
	PreRun: func(cmd *cobra.Command, args []string) {
		validateSupportBundleV2ManifestFile(cmd, args)
		preflightValidateUniverse(cmd)
	},
	Run: runCreateSupportBundleV2,
}

// supportBundleV2RunContext holds shared state for create and estimate subcommands.
type supportBundleV2RunContext struct {
	authAPI     *ybaAuthClient.AuthAPIClient
	universe    ybaclient.UniverseResp
	targetName  string
	targetUUID  string
	ybaOnly     bool
	requestBody ybav2client.SupportBundleCreateSpec
}

// prepareSupportBundleV2Run resolves the manifest, the component selection and the target, and
// builds the create spec. It asks for nothing: confirmation belongs to the command that mutates,
// and is raised there once the request is built and a dry run has had its chance to bow out.
func prepareSupportBundleV2Run(cmd *cobra.Command) (*supportBundleV2RunContext, error) {
	cfg, err := loadSupportBundleV2Config(util.MustGetFlagString(cmd, manifestFileFlag))
	if err != nil {
		return nil, err
	}

	components := selectedComponents(
		cmd, cfg.Spec.NodeComponents, cfg.Spec.GlobalComponents)
	if len(components) == 0 {
		return nil, fmt.Errorf("no components selected for collection")
	}
	warnDisabledNamedComponents(cmd, cfg)

	universeName, err := cmd.Flags().GetString("name")
	if err != nil {
		return nil, err
	}
	ybaOnly := util.IsEmptyString(universeName)
	if ybaOnly {
		if !allComponentsAreYbaScript(cfg, components) {
			return nil, fmt.Errorf(
				"universe name (-n) is required for node-level or universe-scoped components")
		}
	}

	nodeNames := resolveNodeNames(cmd, cfg)
	if ybaOnly && len(nodeNames) > 0 {
		return nil, fmt.Errorf(
			"node names are not applicable to YBA-only support bundles, which collect no " +
				"node-level components")
	}

	var authAPI *ybaAuthClient.AuthAPIClient
	var universe ybaclient.UniverseResp
	var targetName string
	var targetUUID string
	if ybaOnly {
		authAPI = ybaAuthClient.NewAuthAPIClientAndCustomer()
		targetName = "YBA platform host"
		targetUUID = authAPI.CustomerUUID
	} else {
		authAPI, universe, err = universeutil.Validations(cmd, util.SupportBundleOperation)
		if err != nil {
			return nil, err
		}
		targetName = universe.GetName()
		targetUUID = universe.GetUniverseUUID()
	}

	// Warn before the resolved window is written back below, while cfg still holds whatever the
	// manifest carried.
	warnOnManifestWindow(cfg)
	startDate, endDate, err := resolveCollectionTimeRange(cmd)
	if err != nil {
		return nil, err
	}
	cfg.Spec.CollectionTimeRange.StartDate = startDate
	cfg.Spec.CollectionTimeRange.EndDate = endDate
	logrus.Infof("Using collection time range: %s to %s\n", startDate, endDate)
	if len(nodeNames) > 0 {
		logrus.Infof(
			"Restricting node-level components to node(s): %s\n", strings.Join(nodeNames, ", "))
	}

	requestBody, err := buildSupportBundleCreateRequest(cfg, components, startDate, endDate, nodeNames)
	if err != nil {
		return nil, err
	}

	return &supportBundleV2RunContext{
		authAPI:     authAPI,
		universe:    universe,
		targetName:  targetName,
		targetUUID:  targetUUID,
		ybaOnly:     ybaOnly,
		requestBody: requestBody,
	}, nil
}

// confirmTargetLabel names what the create prompt is about to act on.
func confirmTargetLabel(runCtx *supportBundleV2RunContext) string {
	if runCtx.ybaOnly {
		return "the YBA platform host"
	}
	return fmt.Sprintf("universe %s", runCtx.targetName)
}

// runCreateSupportBundleV2 executes the support bundle v2 create subcommand.
func runCreateSupportBundleV2(cmd *cobra.Command, args []string) {
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
		logrus.Infof("DRY-RUN create support bundle request body:\n%s\n", string(pretty))
		return
	}

	// Raised here rather than in prepareSupportBundleV2Run so that a dry run bows out first and
	// the target named in the prompt is one the pre-flight lookup has already resolved.
	if err := util.ConfirmCommand(
		fmt.Sprintf("Are you sure you want to execute support-bundlev2 components on %s",
			confirmTargetLabel(runCtx)),
		viper.GetBool("force"),
	); err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	targetLabel := targetLogLabel(runCtx.ybaOnly, runCtx.targetName, runCtx.targetUUID)
	logrus.Infof("Creating support bundle on %s via the create API\n", targetLabel)

	var task *ybav2client.YBATask
	var response *http.Response
	if runCtx.ybaOnly {
		task, response, err = runCtx.authAPI.CreateYbaSupportBundleV2().
			SupportBundleCreateSpec(runCtx.requestBody).Execute()
	} else {
		task, response, err = runCtx.authAPI.CreateSupportBundleV2(runCtx.targetUUID).
			SupportBundleCreateSpec(runCtx.requestBody).Execute()
	}
	if err != nil {
		util.FatalHTTPError(response, err, "Universe: Support Bundle v2", "Create")
	}
	if task == nil || util.IsEmptyString(task.GetTaskUuid()) {
		logrus.Fatal(formatter.Colorize(
			"unexpected response from create support bundle API: no task returned\n",
			formatter.RedColor))
	}

	bundleUUID := task.GetResourceUuid()
	taskUUID := task.GetTaskUuid()

	logrus.Infof(
		"The support bundle %s for %s is being created (task %s)\n",
		formatter.Colorize(bundleUUID, formatter.GreenColor),
		targetLabel,
		taskUUID,
	)

	if viper.GetBool("wait") {
		msg := fmt.Sprintf(
			"The support bundle %s for %s is being created", bundleUUID, targetLabel)
		if err := runCtx.authAPI.WaitForTask(taskUUID, msg); err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		logrus.Infof(
			"The support bundle %s for %s has been created\n",
			formatter.Colorize(bundleUUID, formatter.GreenColor),
			targetLabel,
		)
		writeCreatedSupportBundle(runCtx, bundleUUID)
		return
	}

	// Matching v1: the task goes to stdout so -o json carries the uuids a script needs.
	taskCtx := formatter.Context{
		Command: "create",
		Output:  os.Stdout,
		Format:  ybatask.NewTaskFormat(viper.GetString("output")),
	}
	if err := ybatask.Write(taskCtx, []ybaclient.YBPTask{taskToClient(task)}); err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

// taskToClient maps the v2 task onto the v1 shape the shared ybatask formatter renders; both carry
// only the task and resource uuids.
func taskToClient(task *ybav2client.YBATask) ybaclient.YBPTask {
	return ybaclient.YBPTask{
		TaskUUID:     task.TaskUuid,
		ResourceUUID: task.ResourceUuid,
	}
}

// writeCreatedSupportBundle prints the finished bundle, as v1 does under --wait.
func writeCreatedSupportBundle(runCtx *supportBundleV2RunContext, bundleUUID string) {
	target := &supportBundleV2Target{
		authAPI:    runCtx.authAPI,
		universe:   runCtx.universe,
		ybaOnly:    runCtx.ybaOnly,
		targetName: runCtx.targetName,
		targetUUID: runCtx.targetUUID,
	}
	bundle, response, err := getSupportBundleV2(target, bundleUUID)
	if err != nil {
		util.FatalHTTPError(response, err, "Universe: Support Bundle v2", "Create - Fetch")
	}
	if !runCtx.ybaOnly {
		supportbundle.Universe = runCtx.universe
	}
	bundleCtx := formatter.Context{
		Command: "create",
		Output:  os.Stdout,
		Format:  supportbundle.NewSupportBundleFormat(viper.GetString("output")),
	}
	if err := supportbundle.Write(bundleCtx, []ybaclient.SupportBundle{bundle}); err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}
