/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// SupportBundleV2Cmd is the root command for support bundle v2 CLI operations.
var SupportBundleV2Cmd = &cobra.Command{
	Use:     "support-bundlev2",
	Aliases: []string{"sb2", "supportbundlev2"},
	Short:   "Support bundle v2 operations",
	Long: "Support bundle v2 operations using the v2 API. Create and estimate bundles from a YAML config; " +
		"list, download, and delete bundles created through this flow. Global-level legacy components " +
		"(YbaMetadata, TabletReport, PrometheusMetrics, PerfAdvisor, K8sInfo) are listed as sibling groups under " +
		"spec.GlobalComponents. Script-driven YBA host components (e.g. ApplicationLogs) live under " +
		"spec.GlobalComponents.YBA and use YBAComponent specs. Node-level components are mapped to " +
		"FilesComponent, BashComponent, YSQLComponent, YCQLComponent, or YbAdminComponent specs.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

// validateSupportBundleV2ManifestFile resolves the manifest to use and writes it back to the
// manifest-file flag, so that the rest of the command reads a path that is known to exist.
func validateSupportBundleV2ManifestFile(cmd *cobra.Command, args []string) {
	viper.BindPFlag("force", cmd.Flags().Lookup("force"))

	manifestFile, err := resolveManifestFile(cmd)
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if err := cmd.Flags().Set(manifestFileFlag, manifestFile); err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	logrus.Infof("Using support bundle v2 manifest %s\n", manifestFile)
}
func registerSupportBundleV2ManifestFlags(cmd *cobra.Command) {
	cmd.Flags().SortFlags = false
	cmd.Flags().String(manifestFileFlag, "",
		"[Optional] Path to the support bundle v2 manifest yaml. Defaults to "+
			manifestPathInDevops+" under the devops directory, located using the DEVOPS_HOME "+
			"environment variable or the yb.devops.home key of the YBA application.conf. "+
			"It selects the components. YBA rejects a component its own manifest lacks or "+
			"whose script or entrypoint differs, and takes each component's arguments, "+
			"queries, timeout, tar path and Linux user from its own manifest.")
	cmd.Flags().String("components", "",
		"[Optional] Comma-separated list of components or component groups to execute "+
			"(overrides the component selection in yaml spec.NodeComponents and "+
			"GlobalComponents). A component that the manifest marks enabled: false stays "+
			"excluded and is reported.")
	cmd.Flags().String("start-time", "",
		"[Optional] Collection window start, strict RFC3339 (2006-01-02T15:04:05Z) as on "+
			"support-bundle. Defaults to 24 hours before --end-time or now.")
	cmd.Flags().String("end-time", "",
		"[Optional] Collection window end, strict RFC3339 (2006-01-02T15:04:05Z), must not be in "+
			"the future. Defaults to the current time.")
	cmd.Flags().String("node-names", "",
		"[Optional] Comma-separated list of universe node names to collect node-level components "+
			"from (overrides yaml spec.nodeSelection.nodeNames). Defaults to all nodes in the "+
			"universe. Global components such as PrometheusMetrics, YbaMetadata, TabletReport, "+
			"PerfAdvisor and ApplicationLogs are collected once for the universe and are not "+
			"scoped by this list. Not applicable to YBA-only support bundles.")
	cmd.Flags().Bool("dry-run", false,
		"[Optional] Print the generated support bundle request without calling the API.")
}
func init() {
	SupportBundleV2Cmd.Flags().SortFlags = false
	SupportBundleV2Cmd.PersistentFlags().SortFlags = false

	SupportBundleV2Cmd.PersistentFlags().StringP("name", "n", "",
		"[Optional] The name of the universe. Omit when operating on YBA-only support bundles "+
			"(e.g. ApplicationLogs) on the YBA platform host.")
	SupportBundleV2Cmd.PersistentFlags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	SupportBundleV2Cmd.PersistentFlags().BoolP("skip-validations", "s", false,
		"[Optional] Skip the universe lookup performed before the command runs. The universe is "+
			"still resolved when the command needs it, so this only forgoes failing early.")

	createSupportBundleV2Cmd.Flags().SortFlags = false
	registerSupportBundleV2ManifestFlags(createSupportBundleV2Cmd)

	estimateSupportBundleV2Cmd.Flags().SortFlags = false
	registerSupportBundleV2ManifestFlags(estimateSupportBundleV2Cmd)

	downloadSupportBundleV2Cmd.Flags().SortFlags = false
	downloadSupportBundleV2Cmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the support bundle to download.")
	downloadSupportBundleV2Cmd.MarkFlagRequired("uuid")
	downloadSupportBundleV2Cmd.Flags().String("path", "",
		"[Optional] Custom directory path to save the downloaded support bundle. "+
			"If not provided, saves to the CLI config directory.")

	deleteSupportBundleV2Cmd.Flags().SortFlags = false
	deleteSupportBundleV2Cmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the support bundle to delete.")
	deleteSupportBundleV2Cmd.MarkFlagRequired("uuid")

	SupportBundleV2Cmd.AddCommand(createSupportBundleV2Cmd)
	SupportBundleV2Cmd.AddCommand(estimateSupportBundleV2Cmd)
	SupportBundleV2Cmd.AddCommand(listSupportBundleV2Cmd)
	SupportBundleV2Cmd.AddCommand(downloadSupportBundleV2Cmd)
	SupportBundleV2Cmd.AddCommand(deleteSupportBundleV2Cmd)
}
