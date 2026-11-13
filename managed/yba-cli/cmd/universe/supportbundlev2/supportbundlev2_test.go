/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
)

// goldenManifest is the manifest shipped under devops, which these tests build specs from rather
// than hand-rolling a config, so they fail when the shipped file and the builders disagree.
const goldenManifest = "../../../../devops/bin/support_bundlev2_manifest.yaml"

func TestBuildYSQLComponentSpecListDatabases(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			NodeComponents: map[string][]string{
				"YSQL": {"list_databases"},
			},
			ComponentConfigs: map[string]supportBundleV2Component{
				"list_databases": {
					Enabled: true,
					Params: map[string]interface{}{
						"dbName":         "yugabyte",
						"outputFileName": "pg_databases.txt",
						"queries": []interface{}{
							"SELECT datname FROM pg_database;",
						},
					},
				},
			},
		},
	}
	componentCfg := cfg.Spec.ComponentConfigs["list_databases"]

	got, err := resolveNodeComponentType(cfg, "list_databases")
	if err != nil {
		t.Fatalf("resolveNodeComponentType() error = %v", err)
	}
	if got != "YSQLComponent" {
		t.Fatalf("resolveNodeComponentType() = %q, want YSQLComponent", got)
	}

	spec, err := buildYSQLComponentSpec(cfg, "list_databases", componentCfg)
	if err != nil {
		t.Fatalf("buildYSQLComponentSpec() error = %v", err)
	}
	if spec.GetComponentName() != "list_databases" {
		t.Fatalf("ComponentName = %q, want list_databases", spec.GetComponentName())
	}
	if spec.GetDbName() != "yugabyte" {
		t.Fatalf("DbName = %q, want yugabyte", spec.GetDbName())
	}
	if spec.GetOutputFileName() != "pg_databases.txt" {
		t.Fatalf("OutputFileName = %q, want pg_databases.txt", spec.GetOutputFileName())
	}
	wantQueries := []string{"SELECT datname FROM pg_database;"}
	if !reflect.DeepEqual(spec.Queries, wantQueries) {
		t.Fatalf("Queries = %#v, want %#v", spec.Queries, wantQueries)
	}
	if spec.GetTimeoutSecs() != 300 {
		t.Fatalf("TimeoutSecs = %d, want 300", spec.GetTimeoutSecs())
	}
}

func TestBuildYCQLComponentSpecListKeyspaces(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			NodeComponents: map[string][]string{
				"YCQL": {"list_keyspaces"},
			},
			ComponentConfigs: map[string]supportBundleV2Component{
				"list_keyspaces": {
					Enabled: true,
					Params: map[string]interface{}{
						"outputFileName": "ycql_keyspaces.txt",
						"queries": []interface{}{
							"SELECT keyspace_name FROM system_schema.keyspaces;",
						},
					},
				},
			},
		},
	}
	componentCfg := cfg.Spec.ComponentConfigs["list_keyspaces"]

	got, err := resolveNodeComponentType(cfg, "list_keyspaces")
	if err != nil {
		t.Fatalf("resolveNodeComponentType() error = %v", err)
	}
	if got != "YCQLComponent" {
		t.Fatalf("resolveNodeComponentType() = %q, want YCQLComponent", got)
	}

	spec, err := buildYCQLComponentSpec(cfg, "list_keyspaces", componentCfg)
	if err != nil {
		t.Fatalf("buildYCQLComponentSpec() error = %v", err)
	}
	if spec.GetComponentName() != "list_keyspaces" {
		t.Fatalf("ComponentName = %q, want list_keyspaces", spec.GetComponentName())
	}
	if spec.GetOutputFileName() != "ycql_keyspaces.txt" {
		t.Fatalf("OutputFileName = %q, want ycql_keyspaces.txt", spec.GetOutputFileName())
	}
	wantQueries := []string{"SELECT keyspace_name FROM system_schema.keyspaces;"}
	if !reflect.DeepEqual(spec.Queries, wantQueries) {
		t.Fatalf("Queries = %#v, want %#v", spec.Queries, wantQueries)
	}
	if spec.GetTimeoutSecs() != 300 {
		t.Fatalf("TimeoutSecs = %d, want 300", spec.GetTimeoutSecs())
	}
}

func TestBuildYbAdminComponentSpecListAllMasters(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			NodeComponents: map[string][]string{
				"YbAdmin": {"list_all_masters"},
			},
			ComponentConfigs: map[string]supportBundleV2Component{
				"list_all_masters": {
					Enabled: true,
					Params: map[string]interface{}{
						"ybAdminCommands": []interface{}{"list_all_masters"},
						"outputFileName":  "yb_admin_masters.txt",
					},
				},
			},
		},
	}
	componentCfg := cfg.Spec.ComponentConfigs["list_all_masters"]

	got, err := resolveNodeComponentType(cfg, "list_all_masters")
	if err != nil {
		t.Fatalf("resolveNodeComponentType() error = %v", err)
	}
	if got != "YbAdminComponent" {
		t.Fatalf("resolveNodeComponentType() = %q, want YbAdminComponent", got)
	}

	spec, err := buildYbAdminComponentSpec(cfg, "list_all_masters", componentCfg)
	if err != nil {
		t.Fatalf("buildYbAdminComponentSpec() error = %v", err)
	}
	if spec.GetComponentName() != "list_all_masters" {
		t.Fatalf("ComponentName = %q, want list_all_masters", spec.GetComponentName())
	}
	if len(spec.YbAdminCommands) != 1 || spec.YbAdminCommands[0] != "list_all_masters" {
		t.Fatalf("YbAdminCommands = %v, want [list_all_masters]", spec.YbAdminCommands)
	}
	if spec.GetOutputFileName() != "yb_admin_masters.txt" {
		t.Fatalf("OutputFileName = %q, want yb_admin_masters.txt", spec.GetOutputFileName())
	}
	if spec.GetTimeoutSecs() != 300 {
		t.Fatalf("TimeoutSecs = %d, want 300", spec.GetTimeoutSecs())
	}
}

func TestBuildYbAdminComponentSpecCommandsList(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			NodeComponents: map[string][]string{
				"YbAdmin": {"yb_admin_info"},
			},
			ComponentConfigs: map[string]supportBundleV2Component{
				"yb_admin_info": {
					Enabled: true,
					Params: map[string]interface{}{
						"ybAdminCommands": []interface{}{
							"list_all_masters",
							"list_all_tablet_servers",
						},
						"outputFileName": "yb_admin_info.txt",
					},
				},
			},
		},
	}
	componentCfg := cfg.Spec.ComponentConfigs["yb_admin_info"]

	spec, err := buildYbAdminComponentSpec(cfg, "yb_admin_info", componentCfg)
	if err != nil {
		t.Fatalf("buildYbAdminComponentSpec() error = %v", err)
	}
	if len(spec.YbAdminCommands) != 2 {
		t.Fatalf("YbAdminCommands len = %d, want 2", len(spec.YbAdminCommands))
	}
	if spec.YbAdminCommands[0] != "list_all_masters" {
		t.Fatalf("YbAdminCommands[0] = %q, want list_all_masters", spec.YbAdminCommands[0])
	}
	if spec.GetOutputFileName() != "yb_admin_info.txt" {
		t.Fatalf("OutputFileName = %q, want yb_admin_info.txt", spec.GetOutputFileName())
	}
}

func TestSupportBundleV2YamlQueryExamples(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	cfg.Spec.RequestBuildTimeUTC = "20260716T203045Z"

	ysqlComponents, ok := cfg.Spec.NodeComponents["YSQL"]
	if !ok {
		t.Fatal("NodeComponents.YSQL missing from support_bundlev2_manifest.yaml")
	}
	if !slices.Contains(ysqlComponents, "ysql_yugabyte_queries") {
		t.Fatalf("NodeComponents.YSQL = %#v, want ysql_yugabyte_queries", ysqlComponents)
	}
	ysqlCfg, ok := cfg.Spec.ComponentConfigs["ysql_yugabyte_queries"]
	if !ok {
		t.Fatal("componentConfigs.ysql_yugabyte_queries missing from support_bundlev2_manifest.yaml")
	}
	if !ysqlCfg.Enabled {
		t.Fatal("componentConfigs.ysql_yugabyte_queries must be enabled")
	}
	got, err := resolveNodeComponentType(cfg, "ysql_yugabyte_queries")
	if err != nil {
		t.Fatalf("resolveNodeComponentType(ysql_yugabyte_queries) error = %v", err)
	}
	if got != "YSQLComponent" {
		t.Fatalf("resolveNodeComponentType(ysql_yugabyte_queries) = %q, want YSQLComponent", got)
	}
	ysqlSpec, err := buildYSQLComponentSpec(cfg, "ysql_yugabyte_queries", ysqlCfg)
	if err != nil {
		t.Fatalf("buildYSQLComponentSpec() error = %v", err)
	}
	if ysqlSpec.GetOutputFileName() != "ysql_yugabyte_queries_output_20260716T203045Z.txt" {
		t.Fatalf("YSQL OutputFileName = %q, want ysql_yugabyte_queries_output_20260716T203045Z.txt",
			ysqlSpec.GetOutputFileName())
	}

	ysqlPlatformCfg, ok := cfg.Spec.ComponentConfigs["ysql_system_platform_queries"]
	if !ok {
		t.Fatal("componentConfigs.ysql_system_platform_queries missing from support_bundlev2_manifest.yaml")
	}
	ysqlPlatformSpec, err := buildYSQLComponentSpec(cfg, "ysql_system_platform_queries", ysqlPlatformCfg)
	if err != nil {
		t.Fatalf("buildYSQLComponentSpec(ysql_system_platform_queries) error = %v", err)
	}
	if ysqlPlatformSpec.GetOutputFileName() != "ysql_system_platform_queries_output_20260716T203045Z.txt" {
		t.Fatalf(
			"YSQL OutputFileName = %q, want ysql_system_platform_queries_output_20260716T203045Z.txt",
			ysqlPlatformSpec.GetOutputFileName(),
		)
	}

	ycqlComponents, ok := cfg.Spec.NodeComponents["YCQL"]
	if !ok {
		t.Fatal("NodeComponents.YCQL missing from support_bundlev2_manifest.yaml")
	}
	if !slices.Contains(ycqlComponents, "ycql_queries") {
		t.Fatalf("NodeComponents.YCQL = %#v, want ycql_queries", ycqlComponents)
	}
	ycqlCfg, ok := cfg.Spec.ComponentConfigs["ycql_queries"]
	if !ok {
		t.Fatal("componentConfigs.ycql_queries missing from support_bundlev2_manifest.yaml")
	}
	if !ycqlCfg.Enabled {
		t.Fatal("componentConfigs.ycql_queries must be enabled")
	}
	got, err = resolveNodeComponentType(cfg, "ycql_queries")
	if err != nil {
		t.Fatalf("resolveNodeComponentType(ycql_queries) error = %v", err)
	}
	if got != "YCQLComponent" {
		t.Fatalf("resolveNodeComponentType(ycql_queries) = %q, want YCQLComponent", got)
	}
	ycqlSpec, err := buildYCQLComponentSpec(cfg, "ycql_queries", ycqlCfg)
	if err != nil {
		t.Fatalf("buildYCQLComponentSpec() error = %v", err)
	}
	if ycqlSpec.GetOutputFileName() != "ycql_queries_output_20260716T203045Z.txt" {
		t.Fatalf("YCQL OutputFileName = %q, want ycql_queries_output_20260716T203045Z.txt", ycqlSpec.GetOutputFileName())
	}

	ybAdminComponents, ok := cfg.Spec.NodeComponents["YbAdmin"]
	if !ok {
		t.Fatal("NodeComponents.YbAdmin missing from support_bundlev2_manifest.yaml")
	}
	if !slices.Contains(ybAdminComponents, "yb_admin_commands") {
		t.Fatalf("NodeComponents.YbAdmin = %#v, want yb_admin_commands", ybAdminComponents)
	}
	ybAdminCfg, ok := cfg.Spec.ComponentConfigs["yb_admin_commands"]
	if !ok {
		t.Fatal("componentConfigs.yb_admin_commands missing from support_bundlev2_manifest.yaml")
	}
	if !ybAdminCfg.Enabled {
		t.Fatal("componentConfigs.yb_admin_commands must be enabled")
	}
	got, err = resolveNodeComponentType(cfg, "yb_admin_commands")
	if err != nil {
		t.Fatalf("resolveNodeComponentType(yb_admin_commands) error = %v", err)
	}
	if got != "YbAdminComponent" {
		t.Fatalf("resolveNodeComponentType(yb_admin_commands) = %q, want YbAdminComponent", got)
	}
	ybAdminSpec, err := buildYbAdminComponentSpec(cfg, "yb_admin_commands", ybAdminCfg)
	if err != nil {
		t.Fatalf("buildYbAdminComponentSpec() error = %v", err)
	}
	if len(ybAdminSpec.YbAdminCommands) == 0 {
		t.Fatal("YbAdminCommands must be non-empty for yb_admin_commands")
	}
	if ybAdminSpec.GetOutputFileName() != "yb_admin_commands_output_20260716T203045Z.txt" {
		t.Fatalf("YbAdmin OutputFileName = %q, want yb_admin_commands_output_20260716T203045Z.txt",
			ybAdminSpec.GetOutputFileName())
	}
}

func TestBuildBashComponentParamsOmitsOutputFileName(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	componentCfg := cfg.Spec.ComponentConfigs["collect_top_ps_pmap_stats"]
	params, err := buildBashComponentParams(cfg, "collect_top_ps_pmap_stats", componentCfg)
	if err != nil {
		t.Fatalf("buildBashComponentParams() error = %v", err)
	}
	if paramsContainFlag(params, "--output_file_name") {
		t.Fatalf("Params = %#v, should not include --output_file_name", params)
	}
	// ${bundleUuid} is part of the path so concurrent bundles on one node cannot collide; the
	// server substitutes both tokens per node.
	want := "/tmp/top-ps-pmap-stats-${bundleUuid}-${nodeName}.tar.gz"
	if paramValue(params, "--remote_tar_path") != want {
		t.Fatalf("--remote_tar_path = %q, want %s", paramValue(params, "--remote_tar_path"), want)
	}
}

// A bash member's own manifest params reach its script as flags, after the shared contract. The
// server's SupportBundleV2ManifestMergerTest pins the same argv, so the two builders agree.
func TestBuildBashComponentParamsPassesMemberParams(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	got, err := buildBashComponentParams(
		cfg, "collect_data_dir_map", cfg.Spec.ComponentConfigs["collect_data_dir_map"])
	if err != nil {
		t.Fatalf("buildBashComponentParams() error = %v", err)
	}
	want := []string{
		"collect_data_dir_map",
		"--remote_tar_path", "/tmp/data-dir-map-${bundleUuid}-${nodeName}.tar.gz",
		"--mount_path", "/mnt/d0",
		"--node_home_dir", "/mnt/d0/yb-data",
		"--yb_home_dir", "/home/yugabyte",
		"--max_listing_lines", "200000",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("buildBashComponentParams() = %#v, want %#v", got, want)
	}

	// The other members set only their tar path and timeout, so they keep the shared contract.
	base := []string{"--remote_tar_path", "--mount_path", "--node_home_dir", "--yb_home_dir"}
	for _, name := range cfg.Spec.NodeComponents["Bash"] {
		if name == "collect_data_dir_map" {
			continue
		}
		params, err := buildBashComponentParams(cfg, name, cfg.Spec.ComponentConfigs[name])
		if err != nil {
			t.Fatalf("buildBashComponentParams(%s) error = %v", name, err)
		}
		flags := []string{}
		for _, p := range params {
			if strings.HasPrefix(p, "--") {
				flags = append(flags, p)
			}
		}
		if !reflect.DeepEqual(flags, base) {
			t.Errorf("%s flags = %#v, want %#v", name, flags, base)
		}
	}
}

func TestResolveNodeComponentTypeBashGroup(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	bashComponents := []string{
		"collect_top_ps_pmap_stats",
		"collect_thp_and_timezone_settings",
		"collect_data_dir_map",
		"collect_threadz_memz_rpcz_stats",
		"collect_heap_snapshot",
		"collect_cdc_state",
	}
	for _, componentName := range bashComponents {
		got, err := resolveNodeComponentType(cfg, componentName)
		if err != nil {
			t.Fatalf("resolveNodeComponentType(%s) error = %v", componentName, err)
		}
		if got != "BashComponent" {
			t.Fatalf("resolveNodeComponentType(%s) = %q, want BashComponent", componentName, got)
		}
	}

	if _, err := resolveNodeComponentType(cfg, "collect_foo"); err == nil {
		t.Fatal("resolveNodeComponentType(collect_foo) should error when not listed under NodeComponents")
	}

	if _, err := resolveNodeComponentType(cfg, "ApplicationLogs"); err == nil {
		t.Fatal("resolveNodeComponentType(ApplicationLogs) should error; YBA global is not a node component group")
	}
}

func TestBuildSupportBundleCreateRequestBashComponents(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		[]string{
			"collect_top_ps_pmap_stats",
			"collect_thp_and_timezone_settings",
			"collect_data_dir_map",
			"collect_threadz_memz_rpcz_stats",
			"collect_heap_snapshot",
			"collect_cdc_state",
		},
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		nil,
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}
	if len(requestBody.Components) != 1 || requestBody.Components[0] != "BashComponent" {
		t.Fatalf("Components = %#v, want [BashComponent]", requestBody.Components)
	}
	if len(requestBody.BashComponentSpecs) != 6 {
		t.Fatalf("BashComponentSpecs length = %d, want 6", len(requestBody.BashComponentSpecs))
	}

	wantNames := map[string]struct{}{
		"collect_top_ps_pmap_stats":         {},
		"collect_thp_and_timezone_settings": {},
		"collect_data_dir_map":              {},
		"collect_threadz_memz_rpcz_stats":   {},
		"collect_heap_snapshot":             {},
		"collect_cdc_state":                 {},
	}
	for _, spec := range requestBody.BashComponentSpecs {
		if _, ok := wantNames[spec.GetComponentName()]; !ok {
			t.Fatalf("unexpected BashComponentSpec componentName = %q", spec.GetComponentName())
		}
		if len(spec.Params) == 0 || spec.Params[0] != spec.GetComponentName() {
			t.Fatalf("Params entrypoint = %#v, want %q", spec.Params, spec.GetComponentName())
		}
		if spec.GetScriptPath() != "bin/support_bundle_utils.sh" {
			t.Fatalf("ScriptPath = %q, want bin/support_bundle_utils.sh", spec.GetScriptPath())
		}
		if !paramsContainFlag(spec.Params, "--remote_tar_path") {
			t.Fatalf("Params = %#v, want --remote_tar_path", spec.Params)
		}
		outputFileFlags := []string{
			"--output_file_name",
			"--rpcz_output_file_name",
			"--memz_output_file_name",
			"--mem_trackers_output_file_name",
			"--threadz_output_file_name",
			"--meta_output_file_name",
			"--tserver_heap_current_by_count_output_file_name",
			"--tserver_heap_peak_by_count_output_file_name",
			"--tserver_heap_current_by_estimated_bytes_output_file_name",
			"--master_heap_current_by_count_output_file_name",
		}
		for _, flag := range outputFileFlags {
			if paramsContainFlag(spec.Params, flag) {
				t.Fatalf("Params = %#v, should not include %s", spec.Params, flag)
			}
		}
		if spec.GetComponentName() == "collect_threadz_memz_rpcz_stats" ||
			spec.GetComponentName() == "collect_heap_snapshot" ||
			spec.GetComponentName() == "collect_cdc_state" {
			if spec.GetComponentName() == "collect_cdc_state" {
				if spec.GetTimeoutSecs() != 300 {
					t.Fatalf("collect_cdc_state TimeoutSecs = %d, want 300", spec.GetTimeoutSecs())
				}
				if paramValue(spec.Params, "--remote_tar_path") !=
					"/tmp/cdc-state-${bundleUuid}-${nodeName}.tar.gz" {
					t.Fatalf(
						"collect_cdc_state remote_tar_path = %q, want bundleUuid template",
						paramValue(spec.Params, "--remote_tar_path"))
				}
			}
			delete(wantNames, spec.GetComponentName())
			continue
		}
		if paramValue(spec.Params, "--mount_path") != "/mnt/d0" {
			t.Fatalf("Params mount_path = %q, want /mnt/d0", paramValue(spec.Params, "--mount_path"))
		}
		if paramValue(spec.Params, "--node_home_dir") != "/mnt/d0/yb-data" {
			t.Fatalf("Params node_home_dir = %q, want /mnt/d0/yb-data", paramValue(spec.Params, "--node_home_dir"))
		}
		if paramValue(spec.Params, "--yb_home_dir") != "/home/yugabyte" {
			t.Fatalf("Params yb_home_dir = %q, want /home/yugabyte", paramValue(spec.Params, "--yb_home_dir"))
		}
		if paramsContainFlag(spec.Params, "--top_process_limit") ||
			paramsContainFlag(spec.Params, "--vmstat_sample_count") ||
			paramsContainFlag(spec.Params, "--mount_point") {
			t.Fatalf("Params = %#v, should not include typed bash flags from manifest", spec.Params)
		}
		delete(wantNames, spec.GetComponentName())
	}
	if len(wantNames) != 0 {
		t.Fatalf("missing BashComponentSpecs for %#v", wantNames)
	}
}

func TestBuildSupportBundleCreateRequestBashGroup(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	cmd := &cobra.Command{}
	cmd.Flags().String("components", "", "")
	if err := cmd.Flags().Set("components", "Bash"); err != nil {
		t.Fatalf("set components flag: %v", err)
	}

	components := selectedComponents(cmd, cfg.Spec.NodeComponents, cfg.Spec.GlobalComponents)
	wantComponents := []string{
		"collect_top_ps_pmap_stats",
		"collect_thp_and_timezone_settings",
		"collect_data_dir_map",
		"collect_threadz_memz_rpcz_stats",
		"collect_heap_snapshot",
		"collect_cdc_state",
	}
	if !reflect.DeepEqual(components, wantComponents) {
		t.Fatalf("selectedComponents(Bash) = %#v, want %#v", components, wantComponents)
	}

	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		components,
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		nil,
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}
	if len(requestBody.BashComponentSpecs) != 6 {
		t.Fatalf("BashComponentSpecs length = %d, want 6", len(requestBody.BashComponentSpecs))
	}
}

func TestNormalizeGlobalComponentsShorthand(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			GlobalComponents: map[string][]string{
				"YBA":               {"ApplicationLogs"},
				"YbaMetadata":       nil,
				"TabletReport":      {},
				"PrometheusMetrics": nil,
				"PerfAdvisor":       nil,
				"K8sInfo":           {},
			},
		},
	}
	normalizeGlobalComponents(cfg)

	if got := cfg.Spec.GlobalComponents["YbaMetadata"]; len(got) != 1 || got[0] != "YbaMetadata" {
		t.Fatalf("YbaMetadata = %#v, want [YbaMetadata]", got)
	}
	if got := cfg.Spec.GlobalComponents["TabletReport"]; len(got) != 1 || got[0] != "TabletReport" {
		t.Fatalf("TabletReport = %#v, want [TabletReport]", got)
	}
	if got := cfg.Spec.GlobalComponents["PrometheusMetrics"]; len(got) != 1 || got[0] != "PrometheusMetrics" {
		t.Fatalf("PrometheusMetrics = %#v, want [PrometheusMetrics]", got)
	}
	if got := cfg.Spec.GlobalComponents["PerfAdvisor"]; len(got) != 1 || got[0] != "PerfAdvisor" {
		t.Fatalf("PerfAdvisor = %#v, want [PerfAdvisor]", got)
	}
	if got := cfg.Spec.GlobalComponents["K8sInfo"]; len(got) != 1 || got[0] != "K8sInfo" {
		t.Fatalf("K8sInfo = %#v, want [K8sInfo]", got)
	}
	got, ok := resolveLegacyGlobalComponent(cfg, "YbaMetadata")
	if !ok || got != "YbaMetadata" {
		t.Fatalf("resolveLegacyGlobalComponent(YbaMetadata) = %q, %v", got, ok)
	}
}

func TestApplicationLogsUsesYBAComponent(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	if !isYbaScriptComponent(cfg, "ApplicationLogs") {
		t.Fatal("ApplicationLogs should resolve as a YBA script component")
	}
	if isYbaScriptComponent(cfg, "YbaMetadata") {
		t.Fatal("YbaMetadata should not resolve as a YBA script component")
	}
	ybaComponents := cfg.Spec.GlobalComponents["YBA"]
	if len(ybaComponents) != 1 || ybaComponents[0] != "ApplicationLogs" {
		t.Fatalf("GlobalComponents[YBA] = %#v, want [ApplicationLogs]", ybaComponents)
	}
	if got := cfg.Spec.GlobalComponents["YbaMetadata"]; len(got) != 1 || got[0] != "YbaMetadata" {
		t.Fatalf("GlobalComponents[YbaMetadata] = %#v, want [YbaMetadata]", got)
	}

	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		[]string{"ApplicationLogs"},
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		nil,
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}
	if len(requestBody.Components) != 1 || requestBody.Components[0] != "YBAComponent" {
		t.Fatalf("Components = %#v, want [YBAComponent]", requestBody.Components)
	}
	if len(requestBody.YbaComponentSpecs) != 1 {
		t.Fatalf("YbaComponentSpecs length = %d, want 1", len(requestBody.YbaComponentSpecs))
	}
	spec := requestBody.YbaComponentSpecs[0]
	if spec.GetComponentName() != "ApplicationLogs" {
		t.Fatalf("YBA spec component = %q, want ApplicationLogs", spec.GetComponentName())
	}
	if spec.GetScriptPath() != "bin/yba_utils.sh" {
		t.Fatalf("ScriptPath = %q, want bin/yba_utils.sh", spec.GetScriptPath())
	}
	if len(requestBody.FilesComponentSpecs) != 0 {
		t.Fatalf("FilesComponentSpecs = %#v, want empty for ApplicationLogs", requestBody.FilesComponentSpecs)
	}
}

// A v1 global needs no CLI change: anything listed outside the YBA group without a script is sent
// under its own name.
func TestUnscriptedGlobalsPassThroughWithoutAList(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	cfg.Spec.GlobalComponents["FutureGlobal"] = []string{"FutureGlobal"}
	cfg.Spec.GlobalComponents["YBA"] = append(cfg.Spec.GlobalComponents["YBA"], "UnscriptedYba")

	for _, name := range []string{"ClusterConfig", "FutureGlobal"} {
		if got, ok := resolveLegacyGlobalComponent(cfg, name); !ok || string(got) != name {
			t.Fatalf("resolveLegacyGlobalComponent(%s) = %q, %v; want %s, true", name, got, ok, name)
		}
	}
	// The YBA group only holds scripts, so even an unscripted member is never a v1 global.
	for _, name := range []string{"ApplicationLogs", "UnscriptedYba", "NotListed"} {
		if got, ok := resolveLegacyGlobalComponent(cfg, name); ok {
			t.Fatalf("resolveLegacyGlobalComponent(%s) = %q, true; want false", name, got)
		}
	}

	// Giving a global a script makes it a script component instead.
	scripted := cfg.Spec.ComponentConfigs["ApplicationLogs"]
	cfg.Spec.ComponentConfigs["ClusterConfig"] = scripted
	if got, ok := resolveLegacyGlobalComponent(cfg, "ClusterConfig"); ok {
		t.Fatalf("resolveLegacyGlobalComponent(scripted ClusterConfig) = %q, true; want false", got)
	}
}

func TestLegacyGlobalYBAComponents(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	legacyComponents := map[string]string{
		"TabletReport":      "TabletReport",
		"YbaMetadata":       "YbaMetadata",
		"PrometheusMetrics": "PrometheusMetrics",
		"PerfAdvisor":       "PerfAdvisor",
		"K8sInfo":           "K8sInfo",
	}
	for componentName, apiType := range legacyComponents {
		got, ok := resolveLegacyGlobalComponent(cfg, componentName)
		if !ok {
			t.Fatalf("%s should resolve as a legacy global component", componentName)
		}
		if string(got) != apiType {
			t.Fatalf("resolveLegacyGlobalComponent(%s) = %q, want %q", componentName, got, apiType)
		}
	}

	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		[]string{"ApplicationLogs", "TabletReport", "YbaMetadata", "PrometheusMetrics", "PerfAdvisor", "K8sInfo"},
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		nil,
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}
	if !containsComponentType(requestBody.Components, ybav2client.YBAComponent) {
		t.Fatalf("Components = %#v, want YBAComponent", requestBody.Components)
	}
	if len(requestBody.YbaComponentSpecs) != 1 ||
		requestBody.YbaComponentSpecs[0].GetComponentName() != "ApplicationLogs" {
		t.Fatalf("YbaComponentSpecs = %#v, want one ApplicationLogs spec", requestBody.YbaComponentSpecs)
	}
	for _, apiType := range legacyComponents {
		if !containsComponentType(
			requestBody.Components, ybav2client.SupportBundleComponentType(apiType)) {
			t.Fatalf("Components = %#v, want %q", requestBody.Components, apiType)
		}
	}
	if len(requestBody.FilesComponentSpecs) != 0 {
		t.Fatalf("FilesComponentSpecs = %#v, want empty for global YBA components", requestBody.FilesComponentSpecs)
	}
	wantPromTypes := []ybav2client.PrometheusMetricsType{
		ybav2client.PROMETHEUS_METRICS_MASTER_EXPORT,
		ybav2client.PROMETHEUS_METRICS_NODE_EXPORT,
		ybav2client.PROMETHEUS_METRICS_PLATFORM,
		ybav2client.PROMETHEUS_METRICS_PROMETHEUS,
		ybav2client.PROMETHEUS_METRICS_TSERVER_EXPORT,
		ybav2client.PROMETHEUS_METRICS_CQL_EXPORT,
		ybav2client.PROMETHEUS_METRICS_YSQL_EXPORT,
	}
	if !reflect.DeepEqual(requestBody.PrometheusMetricsTypes, wantPromTypes) {
		t.Fatalf("PrometheusMetricsTypes = %#v, want %#v", requestBody.PrometheusMetricsTypes, wantPromTypes)
	}
	if requestBody.GetPromExportType() != ybav2client.REMOTE_READ {
		t.Fatalf("PromExportType = %q, want REMOTE_READ", requestBody.GetPromExportType())
	}
	if requestBody.GetPromMetricsFormat() != ybav2client.PROM_CHUNK {
		t.Fatalf("PromMetricsFormat = %q, want PROM_CHUNK", requestBody.GetPromMetricsFormat())
	}
	if requestBody.PromDumpDownSample == nil || !*requestBody.PromDumpDownSample {
		t.Fatalf("PromDumpDownSample = %#v, want true", requestBody.PromDumpDownSample)
	}
	if requestBody.GetPaMetricsFormat() != ybav2client.PROM_CHUNK {
		t.Fatalf("PaMetricsFormat = %q, want PROM_CHUNK", requestBody.GetPaMetricsFormat())
	}
}

func TestApplyPerfAdvisorParams(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		[]string{"PerfAdvisor"},
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		nil,
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}
	if len(requestBody.Components) != 1 || requestBody.Components[0] != "PerfAdvisor" {
		t.Fatalf("Components = %#v, want [PerfAdvisor]", requestBody.Components)
	}
	if requestBody.GetPaMetricsFormat() != ybav2client.PROM_CHUNK {
		t.Fatalf("PaMetricsFormat = %q, want PROM_CHUNK", requestBody.GetPaMetricsFormat())
	}
}

// TestPaMetricsFormatFromManifest covers what validatePerfAdvisorParams used to: the Perf Advisor
// metrics format is now validated where it is parsed, by the generated enum constructor. The field
// name in the error is what keeps it distinguishable from promMetricsFormat.
func TestPaMetricsFormatFromManifest(t *testing.T) {
	tests := []struct {
		name    string
		raw     string
		want    ybav2client.PrometheusMetricsFormat
		wantErr string
	}{
		{name: "valid prom chunk", raw: "PROM_CHUNK", want: ybav2client.PROM_CHUNK},
		{name: "valid promql json", raw: "PROMQL_JSON", want: ybav2client.PROMQL_JSON},
		{name: "normalized", raw: "prom-chunk", want: ybav2client.PROM_CHUNK},
		{name: "invalid format", raw: "INVALID", wantErr: "invalid paMetricsFormat"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := promMetricsFormatFromManifest(tt.raw, "paMetricsFormat")
			if tt.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tt.wantErr) {
					t.Fatalf("promMetricsFormatFromManifest() = %v, want error containing %q",
						err, tt.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatalf("promMetricsFormatFromManifest() error = %v", err)
			}
			if got == nil || *got != tt.want {
				t.Fatalf("promMetricsFormatFromManifest() = %v, want %q", got, tt.want)
			}
		})
	}
}

// TestPromExportTypeFromManifest is the promExportType half of the same move.
func TestPromExportTypeFromManifest(t *testing.T) {
	if got, err := promExportTypeFromManifest("remote read"); err != nil ||
		got == nil || *got != ybav2client.REMOTE_READ {
		t.Fatalf("promExportTypeFromManifest(\"remote read\") = %v, %v, want REMOTE_READ", got, err)
	}
	if _, err := promExportTypeFromManifest("INVALID"); err == nil ||
		!strings.Contains(err.Error(), "invalid promExportType") {
		t.Fatalf("promExportTypeFromManifest(INVALID) error = %v, want invalid promExportType", err)
	}
	if got, err := promExportTypeFromManifest(""); err != nil || got != nil {
		t.Fatalf("promExportTypeFromManifest(\"\") = %v, %v, want nil, nil", got, err)
	}
}

// TestPrometheusMetricsTypesRejectsUnknown pins the fail-fast behavior: an unknown metrics type is
// now rejected locally rather than forwarded for the server to 400.
func TestPrometheusMetricsTypesRejectsUnknown(t *testing.T) {
	got, err := normalizePrometheusMetricsTypes([]string{"master_export", "NODE_EXPORT"})
	if err != nil {
		t.Fatalf("normalizePrometheusMetricsTypes() error = %v", err)
	}
	want := []ybav2client.PrometheusMetricsType{
		ybav2client.PROMETHEUS_METRICS_MASTER_EXPORT,
		ybav2client.PROMETHEUS_METRICS_NODE_EXPORT,
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("normalizePrometheusMetricsTypes() = %v, want %v", got, want)
	}
	if _, err := normalizePrometheusMetricsTypes([]string{"NOT_A_TYPE"}); err == nil ||
		!strings.Contains(err.Error(), "invalid prometheusMetricsTypes entry") {
		t.Fatalf("normalizePrometheusMetricsTypes(NOT_A_TYPE) error = %v, want rejection", err)
	}
}

func TestValidatePrometheusMetricsParams(t *testing.T) {
	downSampleFalse := false
	promQueries := map[string]string{"cpu": "up"}
	tests := []struct {
		name    string
		request ybav2client.SupportBundleCreateSpec
		wantErr string
	}{
		{
			name: "promql defaults",
			request: ybav2client.SupportBundleCreateSpec{
				PromExportType:    ybav2client.PROMQL.Ptr(),
				PromMetricsFormat: ybav2client.PROMQL_JSON.Ptr(),
			},
		},
		{
			name: "remote read chunk",
			request: ybav2client.SupportBundleCreateSpec{
				PromExportType:    ybav2client.REMOTE_READ.Ptr(),
				PromMetricsFormat: ybav2client.PROM_CHUNK.Ptr(),
			},
		},
		{
			name: "promql with prom chunk",
			request: ybav2client.SupportBundleCreateSpec{
				PromExportType:    ybav2client.PROMQL.Ptr(),
				PromMetricsFormat: ybav2client.PROM_CHUNK.Ptr(),
			},
			wantErr: "PROM_CHUNK is not supported when promExportType is PROMQL",
		},
		{
			name: "remote read with prom queries",
			request: ybav2client.SupportBundleCreateSpec{
				PromExportType: ybav2client.REMOTE_READ.Ptr(),
				PromQueries:    &promQueries,
			},
			wantErr: "promQueries are not supported",
		},
		{
			name: "promql without downsampling",
			request: ybav2client.SupportBundleCreateSpec{
				PromExportType:     ybav2client.PROMQL.Ptr(),
				PromMetricsFormat:  ybav2client.PROMQL_JSON.Ptr(),
				PromDumpDownSample: &downSampleFalse,
			},
			wantErr: "promDumpDownSample must be true",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validatePrometheusMetricsParams(&tt.request)
			if tt.wantErr == "" {
				if err != nil {
					t.Fatalf("validatePrometheusMetricsParams() error = %v", err)
				}
				return
			}
			if err == nil || !strings.Contains(err.Error(), tt.wantErr) {
				t.Fatalf("validatePrometheusMetricsParams() = %v, want error containing %q", err, tt.wantErr)
			}
		})
	}
}

func TestApplicationLogsWithNodeComponentRequest(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		[]string{"ApplicationLogs", "UniverseLogs"},
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		nil,
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}
	if !containsComponentType(requestBody.Components, ybav2client.YBAComponent) {
		t.Fatalf("Components = %#v, want YBAComponent", requestBody.Components)
	}
	if !containsComponentType(requestBody.Components, "FilesComponent") {
		t.Fatalf("Components = %#v, want FilesComponent", requestBody.Components)
	}
	if len(requestBody.FilesComponentSpecs) != 1 {
		t.Fatalf("FilesComponentSpecs length = %d, want 1", len(requestBody.FilesComponentSpecs))
	}
	if requestBody.FilesComponentSpecs[0].GetComponentName() != "UniverseLogs" {
		t.Fatalf("Files spec component = %q, want UniverseLogs", requestBody.FilesComponentSpecs[0].GetComponentName())
	}
}

// windowCmd builds a command carrying just the two window flags, set only when non-empty, so a
// case can exercise "flag absent" as distinct from "flag set to empty".
func windowCmd(t *testing.T, startDate, endDate string) *cobra.Command {
	t.Helper()
	cmd := &cobra.Command{}
	cmd.Flags().String("start-time", "", "")
	cmd.Flags().String("end-time", "", "")
	if startDate != "" {
		if err := cmd.Flags().Set("start-time", startDate); err != nil {
			t.Fatalf("setting --start-time: %v", err)
		}
	}
	if endDate != "" {
		if err := cmd.Flags().Set("end-time", endDate); err != nil {
			t.Fatalf("setting --end-time: %v", err)
		}
	}
	return cmd
}

func mustParseCollectionTime(t *testing.T, raw string) time.Time {
	t.Helper()
	parsed, err := parseCollectionTime(raw)
	if err != nil {
		t.Fatalf("parseCollectionTime(%q) error = %v", raw, err)
	}
	return parsed
}

// The four ways the window can be specified. Only the flags are consulted; see
// TestBuildRequestIgnoresManifestCollectionTimeRange for the manifest half of the rule.
func TestResolveCollectionTimeRangeFlagCases(t *testing.T) {
	const (
		fixedStart = "2026-01-10T00:00:00Z"
		fixedEnd   = "2026-01-11T00:00:00Z"
	)

	t.Run("both flags are used as given", func(t *testing.T) {
		start, end, err := resolveCollectionTimeRange(windowCmd(t, fixedStart, fixedEnd))
		if err != nil {
			t.Fatalf("resolveCollectionTimeRange() error = %v", err)
		}
		if start != fixedStart || end != fixedEnd {
			t.Fatalf("window = %s..%s, want %s..%s", start, end, fixedStart, fixedEnd)
		}
	})

	t.Run("end only starts one window before it", func(t *testing.T) {
		start, end, err := resolveCollectionTimeRange(windowCmd(t, "", fixedEnd))
		if err != nil {
			t.Fatalf("resolveCollectionTimeRange() error = %v", err)
		}
		if end != fixedEnd {
			t.Fatalf("end = %s, want %s", end, fixedEnd)
		}
		want := mustParseCollectionTime(t, fixedEnd).Add(-defaultCollectionWindow)
		if got := mustParseCollectionTime(t, start); !got.Equal(want) {
			t.Fatalf("start = %s, want %s", got, want)
		}
	})

	t.Run("start only ends now", func(t *testing.T) {
		before := time.Now().UTC()
		start, end, err := resolveCollectionTimeRange(windowCmd(t, fixedStart, ""))
		if err != nil {
			t.Fatalf("resolveCollectionTimeRange() error = %v", err)
		}
		if start != fixedStart {
			t.Fatalf("start = %s, want %s", start, fixedStart)
		}
		endTime := mustParseCollectionTime(t, end)
		if endTime.Before(before.Add(-time.Minute)) || endTime.After(time.Now().UTC().Add(time.Minute)) {
			t.Fatalf("end = %s, want approximately now", end)
		}
	})

	t.Run("neither flag collects the last window", func(t *testing.T) {
		start, end, err := resolveCollectionTimeRange(windowCmd(t, "", ""))
		if err != nil {
			t.Fatalf("resolveCollectionTimeRange() error = %v", err)
		}
		window := mustParseCollectionTime(t, end).Sub(mustParseCollectionTime(t, start))
		if window < defaultCollectionWindow-time.Minute ||
			window > defaultCollectionWindow+time.Minute {
			t.Fatalf("default window = %v, want ~%v", window, defaultCollectionWindow)
		}
	})
}

// The error strings name the flag the operator typed, so they are asserted rather than just the
// presence of an error.
func TestResolveCollectionTimeRangeRejections(t *testing.T) {
	future := time.Now().UTC().Add(48 * time.Hour)

	tests := []struct {
		name      string
		startDate string
		endDate   string
		wantErr   string
	}{
		{
			name:    "end date in the future",
			endDate: formatCollectionTime(future),
			wantErr: "is in the future",
		},
		{
			name:      "start not before end",
			startDate: "2026-01-11T00:00:00Z",
			endDate:   "2026-01-10T00:00:00Z",
			wantErr:   "must be before --end-time",
		},
		{
			name:      "start in the future with no end flag",
			startDate: formatCollectionTime(future),
			wantErr:   "must be in the past",
		},
		{
			name:      "unparseable start",
			startDate: "last tuesday",
			wantErr:   "invalid --start-time",
		},
		{
			name:    "unparseable end",
			endDate: "yesterday-ish",
			wantErr: "invalid --end-time",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, _, err := resolveCollectionTimeRange(windowCmd(t, tt.startDate, tt.endDate))
			if err == nil {
				t.Fatalf("resolveCollectionTimeRange() error = nil, want %q", tt.wantErr)
			}
			if !strings.Contains(err.Error(), tt.wantErr) {
				t.Fatalf("error = %q, want it to contain %q", err.Error(), tt.wantErr)
			}
		})
	}
}

// A manifest carrying a window from an earlier run must not drive the collection. The dates
// planted below are the commented-out sample values from the shipped manifest, years stale; with
// no dates passed the request has to fall back to the default window instead of using them.
func TestBuildRequestIgnoresManifestCollectionTimeRange(t *testing.T) {
	const (
		staleStart = "2022-12-12T13:07:18Z"
		staleEnd   = "2022-12-13T13:07:18Z"
	)

	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	cfg.Spec.CollectionTimeRange.StartDate = staleStart
	cfg.Spec.CollectionTimeRange.EndDate = staleEnd

	if _, err := buildSupportBundleCreateRequest(
		cfg, []string{"collect_data_dir_map"}, "", "", nil,
	); err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}

	start := cfg.Spec.CollectionTimeRange.StartDate
	end := cfg.Spec.CollectionTimeRange.EndDate
	if start == staleStart || end == staleEnd {
		t.Fatalf("manifest window survived into the request: %s..%s", start, end)
	}
	window := mustParseCollectionTime(t, end).Sub(mustParseCollectionTime(t, start))
	if window < defaultCollectionWindow-time.Minute ||
		window > defaultCollectionWindow+time.Minute {
		t.Fatalf("window = %v, want ~%v", window, defaultCollectionWindow)
	}
}

// warnOnManifestWindow is the operator-facing half of the rule: the dates are ignored, but not
// silently. It must stay quiet for the shipped manifest, whose collectionTimeRange is empty.
func TestWarnOnManifestWindow(t *testing.T) {
	shipped, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	tests := []struct {
		name     string
		cfg      *supportBundleV2Config
		wantWarn bool
	}{
		{name: "nil config", cfg: nil},
		{name: "shipped manifest leaves the block empty", cfg: shipped},
		{
			name: "placeholder is not a date",
			cfg: &supportBundleV2Config{Spec: supportBundleV2Spec{
				CollectionTimeRange: supportBundleV2CollectionTimeRange{
					StartDate: "<start-date>",
					EndDate:   "<end-date>",
				},
			}},
		},
		{
			name: "one real date is enough",
			cfg: &supportBundleV2Config{Spec: supportBundleV2Spec{
				CollectionTimeRange: supportBundleV2CollectionTimeRange{
					EndDate: "2022-12-13T13:07:18Z",
				},
			}},
			wantWarn: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var buf bytes.Buffer
			previous := logrus.StandardLogger().Out
			logrus.SetOutput(&buf)
			defer logrus.SetOutput(previous)

			warnOnManifestWindow(tt.cfg)

			warned := strings.Contains(buf.String(), "Ignoring collectionTimeRange")
			if warned != tt.wantWarn {
				t.Fatalf("warned = %v, want %v (output: %q)", warned, tt.wantWarn, buf.String())
			}
		})
	}
}

func TestResolveNodeNamesDefaultsToAllNodes(t *testing.T) {
	cfg := &supportBundleV2Config{Spec: supportBundleV2Spec{}}
	if got := resolveNodeNames(&cobra.Command{}, cfg); len(got) != 0 {
		t.Fatalf("resolveNodeNames() = %#v, want empty", got)
	}
}

func TestResolveNodeNamesFromYaml(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			NodeSelection: supportBundleV2NodeSelection{
				NodeNames: []string{"n1", "n2"},
			},
		},
	}
	got := resolveNodeNames(&cobra.Command{}, cfg)
	if !reflect.DeepEqual(got, []string{"n1", "n2"}) {
		t.Fatalf("resolveNodeNames() = %#v, want [n1 n2]", got)
	}
}

// The flag has to win outright rather than merge, so a run can narrow a manifest without editing it.
func TestResolveNodeNamesFlagOverridesYaml(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			NodeSelection: supportBundleV2NodeSelection{
				NodeNames: []string{"from-yaml"},
			},
		},
	}
	cmd := &cobra.Command{}
	cmd.Flags().String("node-names", "", "")
	if err := cmd.Flags().Set("node-names", " n1 , n2 ,, n1 "); err != nil {
		t.Fatalf("set node-names flag: %v", err)
	}

	got := resolveNodeNames(cmd, cfg)
	if !reflect.DeepEqual(got, []string{"n1", "n2"}) {
		t.Fatalf("resolveNodeNames() = %#v, want [n1 n2] trimmed and de-duplicated", got)
	}
}

// The shipped manifest ships placeholders rather than real names, which must not reach the API.
func TestResolveNodeNamesIgnoresYamlPlaceholders(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			NodeSelection: supportBundleV2NodeSelection{
				NodeNames: []string{"<node-name>", "  "},
			},
		},
	}
	if got := resolveNodeNames(&cobra.Command{}, cfg); len(got) != 0 {
		t.Fatalf("resolveNodeNames() = %#v, want empty", got)
	}
}

func TestBuildSupportBundleCreateRequestOmitsEmptyNodeNames(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		[]string{"collect_top_ps_pmap_stats"},
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		nil,
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}

	encoded, err := json.Marshal(requestBody)
	if err != nil {
		t.Fatalf("json.Marshal() error = %v", err)
	}
	var decoded map[string]interface{}
	if err := json.Unmarshal(encoded, &decoded); err != nil {
		t.Fatalf("json.Unmarshal() error = %v", err)
	}
	if _, present := decoded["node_names"]; present {
		t.Fatalf("node_names present in request body, want omitted when no nodes are selected")
	}
}

func TestBuildSupportBundleCreateRequestSendsNodeNames(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		[]string{"collect_top_ps_pmap_stats"},
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		[]string{"n1", "n2"},
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}
	if !reflect.DeepEqual(requestBody.NodeNames, []string{"n1", "n2"}) {
		t.Fatalf("NodeNames = %#v, want [n1 n2]", requestBody.NodeNames)
	}

	encoded, err := json.Marshal(requestBody)
	if err != nil {
		t.Fatalf("json.Marshal() error = %v", err)
	}
	if !strings.Contains(string(encoded), `"node_names":["n1","n2"]`) {
		t.Fatalf("encoded request = %s, want snake_case node_names", encoded)
	}
}

func TestBuildUniverseLogsParamsResolution(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	cfg.Spec.CollectionTimeRange.StartDate = "2026-06-18T00:00:00Z"
	cfg.Spec.CollectionTimeRange.EndDate = "2026-06-19T23:59:59Z"
	componentCfg := cfg.Spec.ComponentConfigs["UniverseLogs"]
	params, err := buildUniverseLogsParams(cfg, "UniverseLogs", componentCfg)
	if err != nil {
		t.Fatalf("buildUniverseLogsParams() error = %v", err)
	}
	// The server resolves these from the node's data directory and the log_dir gflag overrides,
	// so the CLI must send them blank rather than guessing a mount path.
	for _, flag := range []string{
		"--mount_path",
		"--node_home_dir",
		"--master_logs_path",
		"--tserver_logs_path",
		"--ynp_log_path",
	} {
		if got := paramValue(params, flag); got != "" {
			t.Fatalf("%s = %q, want blank; the server resolves it", flag, got)
		}
	}
	if got := paramValue(params, "--start_date"); got != cfg.Spec.CollectionTimeRange.StartDate {
		t.Fatalf("--start_date = %q, want %q", got, cfg.Spec.CollectionTimeRange.StartDate)
	}
	if got := len(paramValues(params, "--regex-b64")); got != 0 {
		t.Fatalf("--regex-b64 count = %d, want 0; the server resolves regex patterns", got)
	}
	if got := len(paramValues(params, "--regex")); got != 0 {
		t.Fatalf("--regex count = %d, want 0; the server resolves regex patterns", got)
	}
}

func TestBuildGFlagsParamsResolution(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	componentCfg := cfg.Spec.ComponentConfigs["GFlags"]
	params, err := buildComponentParams(cfg, "GFlags", componentCfg)
	if err != nil {
		t.Fatalf("buildComponentParams(GFlags) error = %v", err)
	}
	// The value is left blank on purpose; the server resolves the YB home dir per universe.
	if got := paramValue(params, "--node_home_dir"); got != "" {
		t.Fatalf("--node_home_dir = %q, want empty", got)
	}
	wantFiles := []string{"master/conf/server.conf", "tserver/conf/server.conf"}
	if got := paramValues(params, "--gflag_file"); !reflect.DeepEqual(got, wantFiles) {
		t.Fatalf("--gflag_file = %#v, want %#v", got, wantFiles)
	}
}

func TestBuildTabletMetaParamsNodeHomeDirResolution(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	componentCfg := cfg.Spec.ComponentConfigs["TabletMeta"]
	params, err := buildComponentParams(cfg, "TabletMeta", componentCfg)
	if err != nil {
		t.Fatalf("buildComponentParams(TabletMeta) error = %v", err)
	}
	// Tablet metadata lives under the data dir, which the server resolves per universe.
	if paramValue(params, "--node_home_dir") != "" ||
		len(paramValues(params, "--node_home_dir")) != 0 {
		t.Fatalf("TabletMeta must not send --node_home_dir, got %#v", params)
	}
	if got := paramValues(params, "--node_data_dir"); !reflect.DeepEqual(got, []string{""}) {
		t.Fatalf("--node_data_dir = %#v, want one blank value", got)
	}
	wantFiles := []string{"master/tablet-meta", "tserver/tablet-meta"}
	if got := paramValues(params, "--tablet_meta_file"); !reflect.DeepEqual(got, wantFiles) {
		t.Fatalf("--tablet_meta_file = %#v, want %#v", got, wantFiles)
	}
}

func TestBuildNodeAgentParamsResolution(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	componentCfg := cfg.Spec.ComponentConfigs["NodeAgent"]
	params, err := buildComponentParams(cfg, "NodeAgent", componentCfg)
	if err != nil {
		t.Fatalf("buildComponentParams(NodeAgent) error = %v", err)
	}
	// The server resolves the home each node agent recorded at install time.
	if got := paramValue(params, "--node_agent_home_dir"); got != "" {
		t.Fatalf("--node_agent_home_dir = %q, want empty", got)
	}
	wantPaths := []string{"logs", "config"}
	if got := paramValues(params, "--node_agent_path"); !reflect.DeepEqual(got, wantPaths) {
		t.Fatalf("--node_agent_path = %#v, want %#v", got, wantPaths)
	}
}

func TestAllComponentsAreYbaScript(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	if !allComponentsAreYbaScript(cfg, []string{"ApplicationLogs"}) {
		t.Fatal("ApplicationLogs should be YBA-only")
	}
	if allComponentsAreYbaScript(cfg, []string{"UniverseLogs"}) {
		t.Fatal("UniverseLogs should not be YBA-only")
	}
	if allComponentsAreYbaScript(cfg, []string{"ApplicationLogs", "UniverseLogs"}) {
		t.Fatal("mixed node and YBA components should not be YBA-only")
	}
}

func paramValue(params []string, flag string) string {
	for i := 0; i < len(params)-1; i++ {
		if params[i] == flag {
			return params[i+1]
		}
	}
	return ""
}

func paramsContainFlag(params []string, flag string) bool {
	for _, p := range params {
		if p == flag {
			return true
		}
	}
	return false
}

func paramValues(params []string, flag string) []string {
	values := make([]string, 0)
	for i := 0; i < len(params)-1; i++ {
		if params[i] == flag {
			values = append(values, params[i+1])
		}
	}
	return values
}

// The v2 API reads the create body as SupportBundleCreateSpec, which is snake_case on the wire.
func TestSupportBundleCreateRequestUsesSnakeCaseWireFormat(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	requestBody, err := buildSupportBundleCreateRequest(
		cfg,
		[]string{"collect_top_ps_pmap_stats"},
		cfg.Spec.CollectionTimeRange.StartDate,
		cfg.Spec.CollectionTimeRange.EndDate,
		nil,
	)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}

	encoded, err := json.Marshal(requestBody)
	if err != nil {
		t.Fatalf("json.Marshal() error = %v", err)
	}
	var decoded map[string]interface{}
	if err := json.Unmarshal(encoded, &decoded); err != nil {
		t.Fatalf("json.Unmarshal() error = %v", err)
	}

	for _, key := range []string{"components", "start_date", "end_date", "bash_component_specs"} {
		if _, ok := decoded[key]; !ok {
			t.Fatalf("create request is missing %q, got %v", key, decoded)
		}
	}

	specs, ok := decoded["bash_component_specs"].([]interface{})
	if !ok || len(specs) == 0 {
		t.Fatalf("bash_component_specs = %v, want a non-empty list", decoded["bash_component_specs"])
	}
	spec, ok := specs[0].(map[string]interface{})
	if !ok {
		t.Fatalf("bash_component_specs[0] = %v, want an object", specs[0])
	}
	for _, key := range []string{"component_name", "script_path", "remote_tar_path", "params"} {
		if _, ok := spec[key]; !ok {
			t.Fatalf("bash component spec is missing %q, got %v", key, spec)
		}
	}
}

// Responses moved from a flat bundle to a { spec, info } pair, which the CLI flattens back for
// the shared v1 formatter.
func TestSupportBundlePagedRespConversion(t *testing.T) {
	body := `{
		"entities": [
			{
				"spec": {
					"components": ["YBAComponent"],
					"start_date": "2024-01-01T00:00:00Z",
					"end_date": "2024-01-02T00:00:00Z",
					"max_core_file_size": 4096,
					"prometheus_metrics_types": ["MASTER_EXPORT"]
				},
				"info": {
					"uuid": "f33e3c9b-75ab-4c30-80ad-cba85646ea39",
					"status": "Success",
					"scope_uuid": "89a46d52-4edd-4736-922a-35177a0b990c",
					"size_in_bytes": 2048
				}
			}
		],
		"has_next": false,
		"has_prev": false,
		"total_count": 1
	}`

	var page ybav2client.SupportBundlePagedResp
	if err := json.Unmarshal([]byte(body), &page); err != nil {
		t.Fatalf("json.Unmarshal() error = %v", err)
	}
	if page.GetTotalCount() != 1 || len(page.Entities) != 1 {
		t.Fatalf("page = %#v, want a single entity", page)
	}

	bundle := supportBundleV2ToClient(page.Entities[0])
	if bundle.BundleUUID != "f33e3c9b-75ab-4c30-80ad-cba85646ea39" {
		t.Fatalf("BundleUUID = %q", bundle.BundleUUID)
	}
	if bundle.Status != "Success" {
		t.Fatalf("Status = %q", bundle.Status)
	}
	if bundle.ScopeUUID != "89a46d52-4edd-4736-922a-35177a0b990c" {
		t.Fatalf("ScopeUUID = %q", bundle.ScopeUUID)
	}
	if bundle.SizeInBytes == nil || *bundle.SizeInBytes != 2048 {
		t.Fatalf("SizeInBytes = %v, want 2048", bundle.SizeInBytes)
	}
	if len(bundle.BundleDetails.Components) != 1 ||
		bundle.BundleDetails.Components[0] != "YBAComponent" {
		t.Fatalf("Components = %#v, want [YBAComponent]", bundle.BundleDetails.Components)
	}
	if bundle.StartDate == nil || bundle.EndDate == nil {
		t.Fatalf("StartDate/EndDate = %v/%v, want both set", bundle.StartDate, bundle.EndDate)
	}
}

// A YBA-only bundle has no universe scope, so the formatter falls back to the customer UUID.
// scope_uuid is nullable, and the server sends it as an explicit null rather than omitting it:
// that leaves the field "set" with an empty value, so a HasScopeUuid check would wrongly skip the
// fallback and blank the Scope column. Both shapes are covered here.
func TestSupportBundleRespFallsBackToCustomerUUID(t *testing.T) {
	tests := []struct {
		name string
		body string
	}{
		{
			name: "scope omitted",
			body: `{
				"info": {
					"uuid": "f33e3c9b-75ab-4c30-80ad-cba85646ea39",
					"status": "Running",
					"customer_uuid": "89a46d52-4edd-4736-922a-35177a0b990c"
				}
			}`,
		},
		{
			name: "scope explicitly null",
			body: `{
				"info": {
					"uuid": "f33e3c9b-75ab-4c30-80ad-cba85646ea39",
					"status": "Running",
					"scope_uuid": null,
					"customer_uuid": "89a46d52-4edd-4736-922a-35177a0b990c"
				}
			}`,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var resp ybav2client.SupportBundle
			if err := json.Unmarshal([]byte(tt.body), &resp); err != nil {
				t.Fatalf("json.Unmarshal() error = %v", err)
			}

			bundle := supportBundleV2ToClient(resp)
			if bundle.ScopeUUID != "89a46d52-4edd-4736-922a-35177a0b990c" {
				t.Fatalf("ScopeUUID = %q, want the customer UUID", bundle.ScopeUUID)
			}
		})
	}
}

func TestBuildSystemLogsParamsExactArgv(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	cfg.Spec.CollectionTimeRange.StartDate = "2026-06-18T00:00:00Z"
	cfg.Spec.CollectionTimeRange.EndDate = "2026-06-19T23:59:59Z"
	params, err := buildSystemLogsParams(cfg, "SystemLogs", cfg.Spec.ComponentConfigs["SystemLogs"])
	if err != nil {
		t.Fatalf("buildSystemLogsParams() error = %v", err)
	}
	// Pinned in full, and mirrored by systemLogsArgvIsExactAndCarriesNoRetiredFlags on the Java
	// side. The two builders have to stay byte identical, and comparing these two literals is the
	// only thing that enforces it.
	want := []string{
		"create_system_logs_bundle",
		"--log_dir", "/var/log",
		"--start_date", "2026-06-18T00:00:00Z",
		"--end_date", "2026-06-19T23:59:59Z",
		"--remote_tar_path", "/tmp/system-logs-${bundleUuid}-${nodeName}.tar.gz",
		"--use_sudo", "true",
	}
	if !reflect.DeepEqual(params, want) {
		t.Fatalf("buildSystemLogsParams() = %#v, want %#v", params, want)
	}
}

func TestBuildSystemLogsParamsSendsNoRegexOrJournaldFlags(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	params, err := buildSystemLogsParams(cfg, "SystemLogs", cfg.Spec.ComponentConfigs["SystemLogs"])
	if err != nil {
		t.Fatalf("buildSystemLogsParams() error = %v", err)
	}
	// The selection regex and the journald toggle come from universe scoped runtime config, which
	// only the server can read. A CLI that sent its own would give bundles that ignore an
	// operator's setting, and would differ from what the UI produces for the same universe.
	for _, flag := range []string{"--regex", "--regex-b64", "--collect_journald"} {
		if paramsContainFlag(params, flag) {
			t.Fatalf("%s must not be sent by the CLI; the server injects it", flag)
		}
	}
}

func TestBuildSystemLogsParamsDropsRetiredFlags(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	params, err := buildSystemLogsParams(cfg, "SystemLogs", cfg.Spec.ComponentConfigs["SystemLogs"])
	if err != nil {
		t.Fatalf("buildSystemLogsParams() error = %v", err)
	}
	// Selection is regex and mtime driven now, so a log name and a rotation cadence mean nothing.
	for _, flag := range []string{"--messages_prefix", "--rotate_window_days"} {
		if paramsContainFlag(params, flag) {
			t.Fatalf("%s is retired and must not be sent", flag)
		}
	}
}

func TestSystemLogsManifestHasNoRetiredParams(t *testing.T) {
	cfg, err := loadSupportBundleV2Config("../../../../devops/bin/support_bundlev2_manifest.yaml")
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	// Caught here rather than by the builder test, which no longer reads these keys at all.
	for _, key := range []string{"messagesPrefix", "rotateWindowDays"} {
		if _, ok := cfg.Spec.ComponentConfigs["SystemLogs"].Params[key]; ok {
			t.Fatalf("manifest still carries retired SystemLogs param %q", key)
		}
	}
}

// The server sends an empty entities array for a universe with no bundles, which must stay a
// successful empty page rather than being mistaken for an error envelope.
func TestParseSupportBundlePageEmptyListIsNotAnError(t *testing.T) {
	page, err := parseSupportBundlePage(
		[]byte(`{"entities":[],"has_next":false,"total_count":0}`))
	if err != nil {
		t.Fatalf("parseSupportBundlePage() error = %v, want nil", err)
	}
	if page.Entities == nil {
		t.Fatal("Entities = nil, want an empty non-nil slice")
	}
	if len(page.Entities) != 0 {
		t.Fatalf("Entities = %#v, want empty", page.Entities)
	}
}

// util.ErrorFromResponseBody dereferences YbaStructuredError.Error without a nil check, so a body
// carrying neither entities nor an error key must not reach it.
func TestParseSupportBundlePageWithoutErrorKeyDoesNotPanic(t *testing.T) {
	page, err := parseSupportBundlePage([]byte(`{}`))
	if err != nil {
		t.Fatalf("parseSupportBundlePage() error = %v, want nil", err)
	}
	if len(page.Entities) != 0 {
		t.Fatalf("Entities = %#v, want empty", page.Entities)
	}
}

// resolveConfigStringSliceParam used to comma-split a scalar string, which shattered any value
// carrying a comma into several invalid ones. The shipped manifest writes these keys as YAML
// lists so it never hit the branch, but --manifest-file means operators supply their own.
func TestResolveConfigStringSliceParamScalarIsOneElement(t *testing.T) {
	vars := map[string]string{"defaults.remoteTmpDir": "/tmp"}

	tests := []struct {
		name string
		raw  interface{}
		want []string
	}{
		{
			name: "sql with commas stays one query",
			raw: "SELECT timestamp, database, query, calls, mean_time, p99 " +
				"FROM slow_queries_data ORDER BY timestamp DESC LIMIT 20;",
			want: []string{
				"SELECT timestamp, database, query, calls, mean_time, p99 " +
					"FROM slow_queries_data ORDER BY timestamp DESC LIMIT 20;",
			},
		},
		{
			name: "ycql in clause stays one query",
			raw:  "SELECT * FROM system_schema.tables WHERE keyspace_name IN ('a','b');",
			want: []string{"SELECT * FROM system_schema.tables WHERE keyspace_name IN ('a','b');"},
		},
		{
			name: "yaml list is unchanged",
			raw:  []interface{}{"SELECT datname FROM pg_database;", "SELECT 1;"},
			want: []string{"SELECT datname FROM pg_database;", "SELECT 1;"},
		},
		{
			name: "templates still resolve",
			raw:  "${defaults.remoteTmpDir}/out.txt",
			want: []string{"/tmp/out.txt"},
		},
		{
			name: "empty string yields nil",
			raw:  "",
			want: nil,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := resolveConfigStringSliceParam(
				map[string]interface{}{"queries": tt.raw}, "queries", vars)
			if !reflect.DeepEqual(got, tt.want) {
				t.Fatalf("resolveConfigStringSliceParam() = %#v, want %#v", got, tt.want)
			}
		})
	}
}

// A comma list for an enum key is no longer silently split, so it now fails where the value is
// validated rather than producing a wrong request. The message has to name the offending value.
func TestPrometheusMetricsTypesRejectsCommaList(t *testing.T) {
	_, err := normalizePrometheusMetricsTypes([]string{"MASTER_EXPORT,NODE_EXPORT"})
	if err == nil {
		t.Fatal("normalizePrometheusMetricsTypes() error = nil, want a rejection")
	}
	if !strings.Contains(err.Error(), "MASTER_EXPORT,NODE_EXPORT") {
		t.Fatalf("error = %q, want it to name the offending value", err.Error())
	}
}

// buildGenericComponentParams is how a manifest adds a component without a Go change. It once
// emitted values verbatim: templates unresolved, lists as Go's "[a b]", remoteTarPathTemplate under
// a flag no script accepts, timeoutSecs leaking into the argv. The last two abort the component,
// since every support_bundle_utils.sh function exits 1 on an unrecognized argument.
func TestBuildGenericComponentParamsFollowsScriptConventions(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			Defaults: supportBundleV2Defaults{
				MountPath:    "/mnt/d0",
				RemoteTmpDir: "/tmp",
			},
			ComponentConfigs: map[string]supportBundleV2Component{
				"custom_component": {
					Enabled: true,
					Script: supportBundleV2Script{
						ScriptFile: "bin/support_bundle_utils.sh",
						Entrypoint: "collect_custom",
					},
					Params: map[string]interface{}{
						"remoteTarPathTemplate": "${defaults.remoteTmpDir}/custom.tar.gz",
						"timeoutSecs":           300,
						"mountPath":             "${defaults.mountPath}",
						"extraPaths":            []interface{}{"a", "b"},
					},
				},
			},
		},
	}

	got := buildGenericComponentParams(cfg, cfg.Spec.ComponentConfigs["custom_component"])

	if got[0] != "collect_custom" {
		t.Fatalf("argv[0] = %q, want the entrypoint", got[0])
	}
	if paramsContainFlag(got, "--remote_tar_path_template") {
		t.Error("argv carries --remote_tar_path_template, which no script accepts")
	}
	if paramValue(got, "--remote_tar_path") != "/tmp/custom.tar.gz" {
		t.Errorf("--remote_tar_path = %q, want the resolved /tmp/custom.tar.gz",
			paramValue(got, "--remote_tar_path"))
	}
	if paramsContainFlag(got, "--timeout_secs") {
		t.Error("argv carries --timeout_secs, which is spec metadata rather than a script argument")
	}
	if paramValue(got, "--mount_path") != "/mnt/d0" {
		t.Errorf("--mount_path = %q, want the resolved /mnt/d0", paramValue(got, "--mount_path"))
	}
	if extras := paramValues(got, "--extra_paths"); !reflect.DeepEqual(extras, []string{"a", "b"}) {
		t.Errorf("--extra_paths = %#v, want one flag per element", extras)
	}
	for _, p := range got {
		if strings.Contains(p, "${") {
			t.Errorf("argv still carries an unresolved template: %q", p)
		}
	}
}

// A param that is present but resolves to nothing keeps its flag, matching what the dedicated
// builders do for paths the server fills in at execution time.
func TestBuildGenericComponentParamsKeepsEmptyValuedFlags(t *testing.T) {
	cfg := &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			ComponentConfigs: map[string]supportBundleV2Component{
				"custom_component": {
					Script: supportBundleV2Script{Entrypoint: "collect_custom"},
					Params: map[string]interface{}{"nodeHomeDir": ""},
				},
			},
		},
	}

	got := buildGenericComponentParams(cfg, cfg.Spec.ComponentConfigs["custom_component"])
	want := []string{"collect_custom", "--node_home_dir", ""}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("buildGenericComponentParams() = %#v, want %#v", got, want)
	}
}

// disabledComponentManifest has one component switched off and one on, both reachable either by
// name or through the Files group.
func disabledComponentManifest() *supportBundleV2Config {
	return &supportBundleV2Config{
		Spec: supportBundleV2Spec{
			NodeComponents: map[string][]string{
				"Files": {"CoreFiles", "UniverseLogs"},
			},
			ComponentConfigs: map[string]supportBundleV2Component{
				"CoreFiles":    {Enabled: false},
				"UniverseLogs": {Enabled: true},
			},
		},
	}
}

func componentsCmd(t *testing.T, override string) *cobra.Command {
	t.Helper()
	cmd := &cobra.Command{}
	registerSupportBundleV2ManifestFlags(cmd)
	if override != "" {
		if err := cmd.Flags().Set("components", override); err != nil {
			t.Fatalf("Set(components) error = %v", err)
		}
	}
	return cmd
}

// A component named outright on --components but switched off in the manifest is dropped by
// buildSupportBundleCreateRequest. It has to be reported, or the bundle silently omits something
// the caller asked for by name.
func TestWarnDisabledNamedComponents(t *testing.T) {
	cfg := disabledComponentManifest()

	tests := []struct {
		name     string
		override string
		want     []string
	}{
		{
			name:     "named disabled component is reported",
			override: "CoreFiles,UniverseLogs",
			want:     []string{"CoreFiles"},
		},
		{
			name:     "group request stays quiet",
			override: "Files",
			want:     nil,
		},
		{
			name:     "no override stays quiet",
			override: "",
			want:     nil,
		},
		{
			name:     "only enabled components stay quiet",
			override: "UniverseLogs",
			want:     nil,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := disabledNamedComponents(componentsCmd(t, tt.override), cfg)
			if !reflect.DeepEqual(got, tt.want) {
				t.Fatalf("disabledNamedComponents() = %#v, want %#v", got, tt.want)
			}
		})
	}
}

// Selection is unchanged by the warning: the disabled component is still excluded from the
// request, and the enabled one alongside it is still collected.
func TestDisabledComponentStillExcludedFromRequest(t *testing.T) {
	cfg := disabledComponentManifest()
	cfg.Spec.ComponentConfigs["UniverseLogs"] = supportBundleV2Component{
		Enabled: true,
		Script: supportBundleV2Script{
			ScriptFile: "bin/support_bundle_utils.sh",
			Entrypoint: "create_universe_logs_bundle",
		},
		Params: map[string]interface{}{"remoteTarPathTemplate": "/tmp/u.tar.gz"},
	}

	requestBody, err := buildSupportBundleCreateRequest(
		cfg, []string{"CoreFiles", "UniverseLogs"},
		"2026-09-21T00:00:00Z", "2026-09-22T00:00:00Z", nil)
	if err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v", err)
	}

	for _, spec := range requestBody.FilesComponentSpecs {
		if spec.GetComponentName() == "CoreFiles" {
			t.Fatal("CoreFiles is disabled but reached the request")
		}
	}
	if len(requestBody.FilesComponentSpecs) != 1 ||
		requestBody.FilesComponentSpecs[0].GetComponentName() != "UniverseLogs" {
		t.Fatalf("FilesComponentSpecs = %#v, want only UniverseLogs",
			requestBody.FilesComponentSpecs)
	}
}

// The archive name comes from the Content-Disposition header, since the v2 API no longer exposes
// the server-side path. filepath.Base can return ".", ".." or the separator, none of which is a
// usable file name, and the fallback has to fire for all three: ".." would otherwise resolve to
// the parent directory and fail as "is a directory".
func TestSupportBundleV2DownloadFileName(t *testing.T) {
	const bundleUUID = "f33e3c9b-75ab-4c30-80ad-cba85646ea39"
	fallback := "support-bundle-" + bundleUUID + ".tar.gz"

	tests := []struct {
		name     string
		filename string
		want     string
	}{
		{
			name:     "server name is used",
			filename: "yb-support-bundle-universe-20260922175020.885-logs.tar.gz",
			want:     "yb-support-bundle-universe-20260922175020.885-logs.tar.gz",
		},
		{name: "empty falls back", filename: "", want: fallback},
		{name: "dot falls back", filename: ".", want: fallback},
		{name: "dotdot falls back", filename: "..", want: fallback},
		{name: "trailing separator falls back", filename: "../", want: fallback},
		{name: "separator falls back", filename: "/", want: fallback},
		// filepath.Base strips the directory components, so a traversal is reduced to its last
		// element and written inside the download directory. Pinned so a later refactor cannot
		// quietly drop that.
		{name: "traversal is reduced to its base", filename: "../../etc/passwd", want: "passwd"},
		{name: "absolute path is reduced", filename: "/etc/passwd", want: "passwd"},
		{name: "leading dots are a legal name", filename: "..foo", want: "..foo"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			resp := &http.Response{Header: http.Header{}}
			if tt.filename != "" {
				resp.Header.Set("Content-Disposition",
					fmt.Sprintf("attachment; filename=%q", tt.filename))
			}
			if got := supportBundleV2DownloadFileName(resp, bundleUUID); got != tt.want {
				t.Fatalf("supportBundleV2DownloadFileName(%q) = %q, want %q",
					tt.filename, got, tt.want)
			}
		})
	}
}

// A percent-encoded header decodes before the guard sees it, so the guard has to hold for the
// decoded value rather than the raw one.
func TestSupportBundleV2DownloadFileNameDecodesEncodedHeader(t *testing.T) {
	const bundleUUID = "f33e3c9b-75ab-4c30-80ad-cba85646ea39"
	resp := &http.Response{Header: http.Header{}}
	resp.Header.Set("Content-Disposition", "attachment; filename*=UTF-8''%2e%2e%2f")

	want := "support-bundle-" + bundleUUID + ".tar.gz"
	if got := supportBundleV2DownloadFileName(resp, bundleUUID); got != want {
		t.Fatalf("supportBundleV2DownloadFileName() = %q, want the fallback %q", got, want)
	}
}

// A missing response must not panic, since the download reports HTTP errors before this runs.
func TestSupportBundleV2DownloadFileNameNilResponse(t *testing.T) {
	const bundleUUID = "f33e3c9b-75ab-4c30-80ad-cba85646ea39"
	want := "support-bundle-" + bundleUUID + ".tar.gz"
	if got := supportBundleV2DownloadFileName(nil, bundleUUID); got != want {
		t.Fatalf("supportBundleV2DownloadFileName(nil) = %q, want %q", got, want)
	}
}

// --skip-validations guards a universe lookup performed before the command body, so that a bad
// --name fails ahead of any confirmation prompt. It was declared but read nowhere; these pin when
// the lookup is worth doing.
func TestShouldPreflightValidate(t *testing.T) {
	tests := []struct {
		name         string
		universeName string
		skip         bool
		want         bool
	}{
		{name: "universe named", universeName: "u1", want: true},
		{name: "skip-validations opts out", universeName: "u1", skip: true, want: false},
		{name: "yba-only has no universe to look up", universeName: "", want: false},
		{name: "yba-only with skip stays off", universeName: "", skip: true, want: false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			cmd := &cobra.Command{}
			cmd.Flags().StringP("name", "n", "", "")
			cmd.Flags().BoolP("skip-validations", "s", false, "")
			if tt.universeName != "" {
				if err := cmd.Flags().Set("name", tt.universeName); err != nil {
					t.Fatalf("Set(name) error = %v", err)
				}
			}
			if tt.skip {
				if err := cmd.Flags().Set("skip-validations", "true"); err != nil {
					t.Fatalf("Set(skip-validations) error = %v", err)
				}
			}
			if got := shouldPreflightValidate(cmd); got != tt.want {
				t.Fatalf("shouldPreflightValidate() = %v, want %v", got, tt.want)
			}
		})
	}
}

// Only create and delete mutate, so only they prompt. estimate reads sizes and returns them, and
// a dry run sends nothing at all; the confirmation moved out of the shared builder into the create
// command so neither of those asks. This pins the wording create uses.
func TestConfirmTargetLabel(t *testing.T) {
	universe := &supportBundleV2RunContext{targetName: "u1"}
	if got, want := confirmTargetLabel(universe), "universe u1"; got != want {
		t.Fatalf("confirmTargetLabel() = %q, want %q", got, want)
	}

	ybaOnly := &supportBundleV2RunContext{ybaOnly: true, targetName: "YBA platform host"}
	if got, want := confirmTargetLabel(ybaOnly), "the YBA platform host"; got != want {
		t.Fatalf("confirmTargetLabel() = %q, want %q", got, want)
	}
}

// The YBA-only path set targetUUID to the customer UUID and printed it where a universe prints its
// universe UUID, so create, estimate and download labelled the host with an id that is neither the
// host's nor the bundle's.
func TestTargetLogLabel(t *testing.T) {
	const universeUUID = "9b8e9c24-1f0e-4f3e-9f1a-2d9a1f6f0b11"
	if got, want := targetLogLabel(false, "u1", universeUUID),
		"u1 ("+universeUUID+")"; got != want {
		t.Fatalf("targetLogLabel(universe) = %q, want %q", got, want)
	}

	customerUUID := "5f2d7a1c-3b44-4a6e-9c8d-7e1b2a3c4d5e"
	got := targetLogLabel(true, "YBA platform host", customerUUID)
	if want := "YBA platform host"; got != want {
		t.Fatalf("targetLogLabel(yba-only) = %q, want %q", got, want)
	}
	if strings.Contains(got, customerUUID) {
		t.Fatalf("targetLogLabel(yba-only) leaked the customer UUID: %q", got)
	}
}

// failingReader yields some bytes and then fails, standing in for a connection that drops partway
// through a download.
type failingReader struct {
	data []byte
	err  error
}

func (f *failingReader) Read(p []byte) (int, error) {
	if len(f.data) > 0 {
		n := copy(p, f.data)
		f.data = f.data[n:]
		return n, nil
	}
	return 0, f.err
}

// partFiles reports the staging files left behind in dir.
func partFiles(t *testing.T, dir string) []string {
	t.Helper()
	entries, err := os.ReadDir(dir)
	if err != nil {
		t.Fatalf("ReadDir(%s) error = %v", dir, err)
	}
	left := make([]string, 0)
	for _, entry := range entries {
		if strings.Contains(entry.Name(), ".part-") {
			left = append(left, entry.Name())
		}
	}
	return left
}

func TestWriteBundleToFile(t *testing.T) {
	dir := t.TempDir()
	const fileName = "support-bundle.tar.gz"
	payload := []byte("bundle bytes")

	filePath, err := writeBundleToFile(bytes.NewReader(payload), dir, fileName)
	if err != nil {
		t.Fatalf("writeBundleToFile() error = %v", err)
	}
	if want := filepath.Join(dir, fileName); filePath != want {
		t.Fatalf("writeBundleToFile() = %q, want %q", filePath, want)
	}

	got, err := os.ReadFile(filePath)
	if err != nil {
		t.Fatalf("ReadFile(%s) error = %v", filePath, err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("file contents = %q, want %q", got, payload)
	}

	info, err := os.Stat(filePath)
	if err != nil {
		t.Fatalf("Stat(%s) error = %v", filePath, err)
	}
	// Staging through os.CreateTemp would otherwise land 0600, where os.Create landed 0644.
	if got := info.Mode().Perm(); got != bundleFileMode {
		t.Fatalf("file mode = %o, want %o", got, bundleFileMode)
	}

	if left := partFiles(t, dir); len(left) > 0 {
		t.Fatalf("staging files left behind: %v", left)
	}
}

// The bug this covers: os.Create truncated the destination up front and the caller reported the
// copy failure with logrus.Fatal, so a dropped connection replaced a good bundle with a fragment
// carrying the final name.
func TestWriteBundleToFileCopyFailureLeavesExistingFileIntact(t *testing.T) {
	dir := t.TempDir()
	const fileName = "support-bundle.tar.gz"
	filePath := filepath.Join(dir, fileName)
	existing := []byte("an earlier, complete bundle")
	if err := os.WriteFile(filePath, existing, bundleFileMode); err != nil {
		t.Fatalf("WriteFile(%s) error = %v", filePath, err)
	}

	body := &failingReader{data: []byte("truncated"), err: errors.New("connection reset by peer")}
	if _, err := writeBundleToFile(body, dir, fileName); err == nil {
		t.Fatal("writeBundleToFile() error = nil, want the copy failure")
	}

	got, err := os.ReadFile(filePath)
	if err != nil {
		t.Fatalf("ReadFile(%s) error = %v", filePath, err)
	}
	if !bytes.Equal(got, existing) {
		t.Fatalf("existing bundle was modified: %q, want %q", got, existing)
	}

	if left := partFiles(t, dir); len(left) > 0 {
		t.Fatalf("staging files left behind: %v", left)
	}
}

func TestWriteBundleToFileCopyFailureWritesNoDestination(t *testing.T) {
	dir := t.TempDir()
	const fileName = "support-bundle.tar.gz"

	body := &failingReader{data: []byte("truncated"), err: errors.New("unexpected EOF")}
	if _, err := writeBundleToFile(body, dir, fileName); err == nil {
		t.Fatal("writeBundleToFile() error = nil, want the copy failure")
	}

	if _, err := os.Stat(filepath.Join(dir, fileName)); !os.IsNotExist(err) {
		t.Fatalf("Stat(%s) error = %v, want not-exist", filepath.Join(dir, fileName), err)
	}
	if left := partFiles(t, dir); len(left) > 0 {
		t.Fatalf("staging files left behind: %v", left)
	}
}

// A completed download replaces whatever is there, as os.Create did.
func TestWriteBundleToFileOverwritesOnSuccess(t *testing.T) {
	dir := t.TempDir()
	const fileName = "support-bundle.tar.gz"
	filePath := filepath.Join(dir, fileName)
	if err := os.WriteFile(filePath, []byte("stale"), bundleFileMode); err != nil {
		t.Fatalf("WriteFile(%s) error = %v", filePath, err)
	}

	payload := []byte("fresh bundle bytes")
	if _, err := writeBundleToFile(bytes.NewReader(payload), dir, fileName); err != nil {
		t.Fatalf("writeBundleToFile() error = %v", err)
	}

	got, err := os.ReadFile(filePath)
	if err != nil {
		t.Fatalf("ReadFile(%s) error = %v", filePath, err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("file contents = %q, want %q", got, payload)
	}
}

// A tar destination must be unique per bundle and per node, so the CLI forwards ${bundleUuid} and
// ${nodeName} as literal tokens for the server to substitute. This pins that every component the
// shipped manifest produces carries the bundle token: a path that no longer varies per bundle is
// accepted happily by the server and makes concurrent bundles overwrite each other's tar.
func TestEveryRemoteTarPathCarriesBundleUUID(t *testing.T) {
	cfg, err := loadSupportBundleV2Config(goldenManifest)
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	checked := 0
	for componentName, componentCfg := range cfg.Spec.ComponentConfigs {
		params, err := buildComponentParams(cfg, componentName, componentCfg)
		if err != nil {
			t.Fatalf("buildComponentParams(%s) error = %v", componentName, err)
		}
		remoteTarPath := extractRemoteTarPath(params)
		if remoteTarPath == "" {
			continue
		}
		checked++
		if !strings.Contains(remoteTarPath, "${bundleUuid}") {
			t.Errorf("%s remote tar path %q does not vary per bundle", componentName, remoteTarPath)
		}
	}
	if checked == 0 {
		t.Fatal("no component produced a --remote_tar_path; the walk is not exercising anything")
	}
}

// requireRemoteTarPath replaced four silent fallbacks (/tmp/system-logs.tar.gz and the like) and
// one builder that emitted an empty --remote_tar_path. Each must now name the missing field.
func TestRemoteTarPathTemplateIsRequired(t *testing.T) {
	components := []string{
		"SystemLogs", "ApplicationLogs", "YbcLogs", "CoreFiles", "UniverseLogs",
		"collect_top_ps_pmap_stats",
	}

	for _, componentName := range components {
		t.Run(componentName, func(t *testing.T) {
			cfg, err := loadSupportBundleV2Config(goldenManifest)
			if err != nil {
				t.Fatalf("loadSupportBundleV2Config() error = %v", err)
			}
			componentCfg, ok := cfg.Spec.ComponentConfigs[componentName]
			if !ok {
				t.Fatalf("manifest has no componentConfigs entry for %s", componentName)
			}
			delete(componentCfg.Params, "remoteTarPathTemplate")

			params, err := buildComponentParams(cfg, componentName, componentCfg)
			if err == nil {
				t.Fatalf("buildComponentParams() error = nil, params = %#v, want a failure",
					params)
			}
			if want := componentName + " requires params.remoteTarPathTemplate"; err.Error() != want {
				t.Fatalf("error = %q, want %q", err.Error(), want)
			}
		})
	}
}

// flattenComponentGroups used to walk an 18-entry hardcoded list and append anything unrecognised
// afterwards. It now walks the manifest's own keys, so a group the Go code has never heard of is an
// ordinary group rather than a leftover.
func TestFlattenComponentGroupsCollectsUnknownGroups(t *testing.T) {
	groups := map[string][]string{
		"Files":  {"SystemLogs", "UniverseLogs"},
		"Bash":   {"collect_data_dir_map"},
		"Custom": {"collect_something_new"},
	}

	got := flattenComponentGroups(groups)
	want := []string{
		"collect_data_dir_map", "collect_something_new", "SystemLogs", "UniverseLogs",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("flattenComponentGroups() = %#v, want %#v", got, want)
	}
}

// Group position does not reach the request: each group feeds its own per-type spec list and the
// components array is sorted. Pinned because the sorted walk replaced an explicit order, and the
// next reader will want to know whether that order mattered.
func TestFlattenComponentGroupsOrderDoesNotDependOnGroupNames(t *testing.T) {
	underOneName := flattenComponentGroups(map[string][]string{
		"Bash": {"collect_data_dir_map"}, "Files": {"SystemLogs"},
	})
	underAnother := flattenComponentGroups(map[string][]string{
		"Zebra": {"collect_data_dir_map"}, "Alpha": {"SystemLogs"},
	})
	if len(underOneName) != len(underAnother) {
		t.Fatalf("group renaming changed the component count: %#v vs %#v",
			underOneName, underAnother)
	}
}

// The error used to name a fixed "(Files, Bash, YSQL, YCQL, or YbAdmin)" whatever the manifest
// held. A manifest that renames a group must not be told to use the group it just renamed.
func TestResolveNodeComponentTypeErrorNamesTheManifestsGroups(t *testing.T) {
	cfg, err := loadSupportBundleV2Config(goldenManifest)
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	renamed := cfg.Spec.NodeComponents["Bash"]
	delete(cfg.Spec.NodeComponents, "Bash")
	cfg.Spec.NodeComponents["Scripts"] = renamed

	_, err = resolveNodeComponentType(cfg, "collect_data_dir_map")
	if err == nil {
		t.Fatal("resolveNodeComponentType() error = nil, want a failure for the renamed group")
	}
	if strings.Contains(err.Error(), "Bash") {
		t.Fatalf("error names a group the manifest no longer has: %q", err.Error())
	}
	for _, groupName := range []string{"Files", "YCQL", "YSQL", "YbAdmin"} {
		if !strings.Contains(err.Error(), groupName) {
			t.Fatalf("error = %q, missing the manifest's %s group", err.Error(), groupName)
		}
	}
}

// isNodeBashComponent asked for the literal group name "Bash". It now asks which group maps to
// BashComponent, so the question is answered by nodeGroupToComponentType rather than by a spelling
// repeated at the call site.
func TestIsNodeBashComponent(t *testing.T) {
	cfg, err := loadSupportBundleV2Config(goldenManifest)
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	if !isNodeBashComponent(cfg, "collect_data_dir_map") {
		t.Fatal("isNodeBashComponent() = false for a component under the Bash group")
	}
	if isNodeBashComponent(cfg, "SystemLogs") {
		t.Fatal("isNodeBashComponent() = true for a Files component")
	}
}

// The switch over the resolved component type used to treat anything unrecognised as a Files
// component, so a type added to nodeGroupToComponentType without a matching case was collected
// silently under the wrong executor. It must fail instead.
func TestUnknownComponentTypeIsAnError(t *testing.T) {
	cfg, err := loadSupportBundleV2Config(goldenManifest)
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}

	nodeGroupToComponentType["Scripts"] = "ScriptsComponent"
	defer delete(nodeGroupToComponentType, "Scripts")
	cfg.Spec.NodeComponents["Scripts"] = []string{"collect_data_dir_map"}
	delete(cfg.Spec.NodeComponents, "Bash")

	_, err = buildSupportBundleCreateRequest(
		cfg, []string{"collect_data_dir_map"}, "2026-09-21T00:00:00Z", "2026-09-22T00:00:00Z", nil)
	if err == nil {
		t.Fatal("buildSupportBundleCreateRequest() error = nil, want the unknown-type failure")
	}
	if !strings.Contains(err.Error(), "unknown component type") {
		t.Fatalf("error = %q, want it to name the unknown component type", err.Error())
	}
}

// buildYbaComponentSpec ignored params.timeoutSecs, so an ApplicationLogs timeout was dropped.
func TestBuildYbaComponentSpecHonoursPerComponentTimeout(t *testing.T) {
	cfg, err := loadSupportBundleV2Config(goldenManifest)
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	componentCfg, ok := cfg.Spec.ComponentConfigs["ApplicationLogs"]
	if !ok {
		t.Fatal("manifest has no componentConfigs entry for ApplicationLogs")
	}

	spec, err := buildYbaComponentSpec(cfg, "ApplicationLogs", componentCfg)
	if err != nil {
		t.Fatalf("buildYbaComponentSpec() error = %v", err)
	}
	if spec.TimeoutSecs != cfg.Spec.Execution.TimeoutSecs {
		t.Fatalf("TimeoutSecs = %d, want the global %d",
			spec.TimeoutSecs, cfg.Spec.Execution.TimeoutSecs)
	}

	if componentCfg.Params == nil {
		componentCfg.Params = map[string]interface{}{}
	}
	componentCfg.Params["timeoutSecs"] = 45
	spec, err = buildYbaComponentSpec(cfg, "ApplicationLogs", componentCfg)
	if err != nil {
		t.Fatalf("buildYbaComponentSpec() error = %v", err)
	}
	if spec.TimeoutSecs != 45 {
		t.Fatalf("TimeoutSecs = %d, want the per-component 45", spec.TimeoutSecs)
	}
}

// A param that is present but unparseable used to return the default, so an edited manifest could
// have a value silently ignored.
func TestInvalidParamValueIsAnError(t *testing.T) {
	tests := []struct {
		name      string
		component string
		key       string
		value     interface{}
	}{
		{name: "int", component: "CoreFiles", key: "maxNumRecentCores", value: "lots"},
		{name: "int64", component: "CoreFiles", key: "maxCoreFileSize", value: "25GB"},
		{name: "bool", component: "SystemLogs", key: "useSudo", value: "maybe"},
		{name: "timeout", component: "ysql_yugabyte_queries", key: "timeoutSecs", value: "10m"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			cfg, err := loadSupportBundleV2Config(goldenManifest)
			if err != nil {
				t.Fatalf("loadSupportBundleV2Config() error = %v", err)
			}
			componentCfg := cfg.Spec.ComponentConfigs[tt.component]
			if componentCfg.Params == nil {
				componentCfg.Params = map[string]interface{}{}
			}
			componentCfg.Params[tt.key] = tt.value
			cfg.Spec.ComponentConfigs[tt.component] = componentCfg

			_, err = buildSupportBundleCreateRequest(
				cfg, []string{tt.component}, "2026-09-21T00:00:00Z", "2026-09-22T00:00:00Z", nil)
			if err == nil {
				t.Fatalf("buildSupportBundleCreateRequest() error = nil, want %s=%v rejected",
					tt.key, tt.value)
			}
			if !strings.Contains(err.Error(), tt.key) {
				t.Fatalf("error = %q, want it to name %s", err.Error(), tt.key)
			}
		})
	}
}

// A missing key still takes the default.
func TestMissingParamTakesTheDefault(t *testing.T) {
	cfg, err := loadSupportBundleV2Config(goldenManifest)
	if err != nil {
		t.Fatalf("loadSupportBundleV2Config() error = %v", err)
	}
	componentCfg := cfg.Spec.ComponentConfigs["CoreFiles"]
	delete(componentCfg.Params, "maxNumRecentCores")
	cfg.Spec.ComponentConfigs["CoreFiles"] = componentCfg

	if _, err := buildSupportBundleCreateRequest(
		cfg, []string{"CoreFiles"}, "2026-09-21T00:00:00Z", "2026-09-22T00:00:00Z", nil); err != nil {
		t.Fatalf("buildSupportBundleCreateRequest() error = %v, want the default to apply", err)
	}
}

// list, download and delete switch to the YBA-only endpoints when -n is absent; without this the
// switch was silent.
func TestIsYbaOnlyTargetReportsTheScope(t *testing.T) {
	tests := []struct {
		name         string
		universeName string
		wantYbaOnly  bool
		wantLogged   bool
	}{
		{name: "no name switches to yba-only and says so", wantYbaOnly: true, wantLogged: true},
		{name: "named universe stays quiet", universeName: "u1"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			cmd := &cobra.Command{}
			cmd.Flags().StringP("name", "n", "", "")
			if tt.universeName != "" {
				if err := cmd.Flags().Set("name", tt.universeName); err != nil {
					t.Fatalf("Set(name) error = %v", err)
				}
			}

			var buf bytes.Buffer
			previous := logrus.StandardLogger().Out
			logrus.SetOutput(&buf)
			defer logrus.SetOutput(previous)

			ybaOnly, err := isYbaOnlyTarget(cmd)
			if err != nil {
				t.Fatalf("isYbaOnlyTarget() error = %v", err)
			}
			if ybaOnly != tt.wantYbaOnly {
				t.Fatalf("isYbaOnlyTarget() = %v, want %v", ybaOnly, tt.wantYbaOnly)
			}
			logged := strings.Contains(buf.String(), "YBA-only support bundles")
			if logged != tt.wantLogged {
				t.Fatalf("logged = %v (%q), want %v", logged, buf.String(), tt.wantLogged)
			}
		})
	}
}

// create writes the task to stdout so -o json carries the uuids; the shared formatter takes the v1
// shape, so the v2 task has to be mapped onto it.
func TestTaskToClient(t *testing.T) {
	const taskUUID = "7b259089-7ac1-4381-9c5c-ddb43510b2f4"
	const bundleUUID = "af945b4c-1ef1-4d1c-b6a2-14b2b9886ab5"

	task := &ybav2client.YBATask{
		TaskUuid:     ybav2client.PtrString(taskUUID),
		ResourceUuid: ybav2client.PtrString(bundleUUID),
	}
	got := taskToClient(task)
	if got.GetTaskUUID() != taskUUID {
		t.Fatalf("TaskUUID = %q, want %q", got.GetTaskUUID(), taskUUID)
	}
	if got.GetResourceUUID() != bundleUUID {
		t.Fatalf("ResourceUUID = %q, want %q", got.GetResourceUUID(), bundleUUID)
	}

	empty := taskToClient(&ybav2client.YBATask{})
	if empty.GetTaskUUID() != "" || empty.GetResourceUUID() != "" {
		t.Fatalf("empty task = %#v, want both uuids empty", empty)
	}
}

// The window flags take strict RFC3339, as v1's --start-time/--end-time do. The zoneless forms
// accepted before were read as UTC, silently shifting the window for anyone who meant local time.
func TestParseCollectionTimeIsStrictRFC3339(t *testing.T) {
	accepted := []string{
		"2026-09-21T14:00:00Z",
		"2026-09-21T14:00:00+05:30",
		"2026-09-21T14:00:00.500Z",
	}
	for _, raw := range accepted {
		t.Run("accepts "+raw, func(t *testing.T) {
			if _, err := parseCollectionTime(raw); err != nil {
				t.Fatalf("parseCollectionTime(%q) error = %v, want it accepted", raw, err)
			}
		})
	}

	rejected := []string{
		"2026-09-21",
		"2026-09-21 14:00:00",
		"21-09-2026T14:00:00Z",
		"not a time",
	}
	for _, raw := range rejected {
		t.Run("rejects "+raw, func(t *testing.T) {
			_, err := parseCollectionTime(raw)
			if err == nil {
				t.Fatalf("parseCollectionTime(%q) error = nil, want it rejected", raw)
			}
			if !strings.Contains(err.Error(), "RFC3339") {
				t.Fatalf("error = %q, want it to name RFC3339", err.Error())
			}
		})
	}
}

// An offset input still reaches the payload as canonical UTC, so the wire format does not depend on
// what the operator typed.
func TestCollectionWindowReachesThePayloadAsUTC(t *testing.T) {
	parsed, err := parseCollectionTime("2026-09-21T14:00:00+05:30")
	if err != nil {
		t.Fatalf("parseCollectionTime() error = %v", err)
	}
	if got, want := formatCollectionTime(parsed), "2026-09-21T08:30:00Z"; got != want {
		t.Fatalf("formatCollectionTime() = %q, want %q", got, want)
	}
}
