/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryconfig

import (
	"encoding/json"
	"fmt"
	"sort"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const allowOTLPRuntimeConfigKey = "yb.telemetry.allow_otlp"

// The all-zero UUID is the global runtime config scope.
const globalRuntimeConfigScope = "00000000-0000-0000-0000-000000000000"

// Mirrors OtelCollectorUtil.K8S_SUPPORTED_SCRAPE_TARGETS: on Kubernetes the collector is a
// sidecar in the DB pods, so node level targets have nothing to scrape.
var k8sSupportedScrapeTargets = map[string]bool{
	"MASTER_EXPORT":  true,
	"TSERVER_EXPORT": true,
	"YSQL_EXPORT":    true,
	"CQL_EXPORT":     true,
	"OTEL_EXPORT":    true,
}

// sinkCapability is one row of the server's ProviderType signal matrix.
type sinkCapability struct {
	allowedForLogs    bool
	allowedForMetrics bool
}

// Fallback for YBA versions below the provider types endpoint. A provider type absent from
// this table is not validated locally.
var staticSinkCapabilities = map[string]sinkCapability{
	util.DataDogTelemetryProviderType:            {true, true},
	util.OTLPTelemetryProviderType:               {true, true},
	util.SplunkTelemetryProviderType:             {true, false},
	util.AWSCloudWatchTelemetryProviderType:      {true, false},
	util.GCPCloudMonitoringTelemetryProviderType: {true, false},
	util.S3TelemetryProviderType:                 {true, false},
	util.LokiTelemetryProviderType:               {true, false},
	util.DynatraceTelemetryProviderType:          {false, true},
}

// ExporterInfo is the resolved identity of one telemetry provider referenced by a config.
type ExporterInfo struct {
	UUID string
	Name string
	Type string
}

// ResolveExporters rewrites every exporter_uuid given as a provider name into its UUID and
// returns what each resolved to. The API only accepts UUIDs; accepting names keeps the
// get/edit/set round trip usable by hand.
func ResolveExporters(
	authAPI *ybaAuthClient.AuthAPIClient,
	config *ybav2client.TelemetryConfig,
) map[string]ExporterInfo {
	resolved := make(map[string]ExporterInfo)
	refs := make([]*string, 0)
	for _, def := range sectionDefs {
		_, sectionRefs := def.refs(config)
		refs = append(refs, sectionRefs...)
	}
	if len(refs) == 0 {
		return resolved
	}

	providers := telemetryproviderutil.ListAndFilterTelemetryProviders(
		authAPI, "Universe: Telemetry Config", "Set", "", "")

	byUUID := make(map[string]util.TelemetryProvider, len(providers))
	byName := make(map[string][]util.TelemetryProvider, len(providers))
	for _, provider := range providers {
		byUUID[provider.GetUuid()] = provider
		byName[provider.GetName()] = append(byName[provider.GetName()], provider)
	}

	for _, ref := range refs {
		value := strings.TrimSpace(*ref)
		if util.IsEmptyString(value) {
			logrus.Fatal(formatter.Colorize(
				"Every exporter requires an exporter_uuid, set to a telemetry provider "+
					"UUID or name\n",
				formatter.RedColor))
		}

		// UUID first, so a provider named after another provider's UUID cannot
		// redirect the exporter.
		match, found := byUUID[value]
		if !found {
			matches := byName[value]
			if len(matches) == 0 {
				logrus.Fatal(formatter.Colorize(
					"No telemetry provider found with UUID or name "+value+
						". Run \"yba telemetry-provider list\" to see the available "+
						"providers\n",
					formatter.RedColor))
			}
			if len(matches) > 1 {
				logrus.Fatal(formatter.Colorize(
					"More than one telemetry provider is named "+value+
						". Use the exporter UUID instead\n",
					formatter.RedColor))
			}
			match = matches[0]
			logrus.Debugf("Resolved exporter %s to %s\n", value, match.GetUuid())
		}

		*ref = match.GetUuid()
		providerConfig := match.GetConfig()
		resolved[match.GetUuid()] = ExporterInfo{
			UUID: match.GetUuid(),
			Name: match.GetName(),
			Type: providerConfig.GetType(),
		}
	}

	return resolved
}

// Prefers the server's provider types endpoint so a type added after this CLI release is
// still judged correctly.
func sinkCapabilities(authAPI *ybaAuthClient.AuthAPIClient) map[string]sinkCapability {
	allowed, _, err := authAPI.TelemetryProviderTypesYBAVersionCheck()
	if err != nil || !allowed {
		return staticSinkCapabilities
	}
	types, response, err := authAPI.ListTelemetryProviderTypes().Execute()
	if err != nil {
		logrus.Debugf(
			"Could not list telemetry provider types (%s); using the built-in table\n",
			util.ErrorFromHTTPResponse(
				response, err, "Telemetry Provider", "List Types").Error())
		return staticSinkCapabilities
	}
	capabilities := make(map[string]sinkCapability, len(types))
	for _, providerType := range types {
		if providerType.ProviderType == nil {
			continue
		}
		capabilities[*providerType.ProviderType] = sinkCapability{
			allowedForLogs:    providerType.GetIsAllowedForLogs(),
			allowedForMetrics: providerType.GetIsAllowedForMetrics(),
		}
	}
	if len(capabilities) == 0 {
		return staticSinkCapabilities
	}
	return capabilities
}

// ValidateExporters rejects an exporter referenced twice from one section, and one whose
// provider type cannot carry that section's signal.
func ValidateExporters(
	authAPI *ybaAuthClient.AuthAPIClient,
	config *ybav2client.TelemetryConfig,
	resolved map[string]ExporterInfo,
) {
	capabilities := sinkCapabilities(authAPI)

	for _, section := range Sections(config) {
		seen := make(map[string]bool, len(section.ExporterUUIDs))
		for _, exporterUUID := range section.ExporterUUIDs {
			info := resolved[exporterUUID]
			if seen[exporterUUID] {
				logrus.Fatal(formatter.Colorize(
					fmt.Sprintf(
						"Section %s references telemetry provider %s more than once\n",
						section.Name, describeExporter(exporterUUID, info)),
					formatter.RedColor))
			}
			seen[exporterUUID] = true

			capability, tracked := capabilities[info.Type]
			if !tracked {
				continue
			}
			allowed := capability.allowedForLogs
			if section.Signal == signalMetrics {
				allowed = capability.allowedForMetrics
			}
			if allowed {
				continue
			}
			logrus.Fatal(formatter.Colorize(
				fmt.Sprintf(
					"Section %s exports %s, but telemetry provider %s is a %s sink. "+
						"Reference it from %s instead\n",
					section.Name, section.Signal,
					describeExporter(exporterUUID, info),
					signalDescription(capability),
					otherSignalSections(section.Signal)),
				formatter.RedColor))
		}
	}
}

func describeExporter(exporterUUID string, info ExporterInfo) string {
	if util.IsEmptyString(info.Name) {
		return exporterUUID
	}
	return fmt.Sprintf("%s (%s, %s)", info.Name, info.Type, exporterUUID)
}

func signalDescription(capability sinkCapability) string {
	switch {
	case capability.allowedForLogs && capability.allowedForMetrics:
		return "logs and metrics"
	case capability.allowedForLogs:
		return "logs-only"
	case capability.allowedForMetrics:
		return "metrics-only"
	default:
		return "disabled"
	}
}

func otherSignalSections(signal string) string {
	if signal == signalMetrics {
		return "a log section (audit_logs, query_logs or a server log section)"
	}
	return "the metrics section"
}

// ValidateServerLogsSupport fails when the document uses one of the six server log sections
// against a YBA that only understands audit_logs, query_logs and metrics.
func ValidateServerLogsSupport(
	authAPI *ybaAuthClient.AuthAPIClient,
	config *ybav2client.TelemetryConfig,
) {
	used := make([]string, 0)
	for _, section := range Sections(config) {
		if section.ServerLog && section.Present {
			used = append(used, section.Name)
		}
	}
	if len(used) == 0 {
		return
	}
	allowed, version, err := authAPI.ServerLogsExportYBAVersionCheck()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if !allowed {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"Server log export sections (%s) are not supported by YBA version %s. "+
					"They require stable %s or preview %s\n",
				strings.Join(used, ", "), version,
				util.YBAAllowServerLogsExportMinStableVersion,
				util.YBAAllowServerLogsExportMinPreviewVersion),
			formatter.RedColor))
	}
}

// ValidateKubernetesSupport enforces the two Kubernetes restrictions the server applies:
// export types whose source runs on the VM, and unreachable metric scrape targets.
func ValidateKubernetesSupport(
	config *ybav2client.TelemetryConfig,
	universe ybaclient.UniverseResp,
) {
	if !isKubernetesUniverse(universe) {
		return
	}

	unsupported := make([]string, 0)
	for _, section := range Sections(config) {
		if section.Present && !section.SupportedOnKubernetes {
			unsupported = append(unsupported, section.Name)
		}
	}
	if len(unsupported) > 0 {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"Export types %s are not supported on Kubernetes universes, because their "+
					"source runs on the VM rather than in the yb-master or yb-tserver "+
					"pods the collector sidecar can reach. Remove the sections\n",
				strings.Join(unsupported, ", ")),
			formatter.RedColor))
	}

	if config.Metrics == nil {
		return
	}
	// An empty list means every target server side, which on Kubernetes includes the node
	// level ones, so it has to be spelled out rather than merely filtered.
	if len(config.Metrics.ScrapeConfigTargets) == 0 {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"metrics.scrape_config_targets must be set explicitly on a Kubernetes "+
					"universe, since an empty list means every target. Supported "+
					"targets: %s\n",
				strings.Join(sortedK8sScrapeTargets(), ", ")),
			formatter.RedColor))
	}
	unsupportedTargets := make([]string, 0)
	for _, target := range config.Metrics.ScrapeConfigTargets {
		if !k8sSupportedScrapeTargets[string(target)] {
			unsupportedTargets = append(unsupportedTargets, string(target))
		}
	}
	if len(unsupportedTargets) > 0 {
		sort.Strings(unsupportedTargets)
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"metrics.scrape_config_targets %s cannot be scraped on a Kubernetes "+
					"universe. Supported targets: %s\n",
				strings.Join(unsupportedTargets, ", "),
				strings.Join(sortedK8sScrapeTargets(), ", ")),
			formatter.RedColor))
	}
}

func isKubernetesUniverse(universe ybaclient.UniverseResp) bool {
	universeDetails := universe.GetUniverseDetails()
	for _, cluster := range universeDetails.GetClusters() {
		userIntent := cluster.GetUserIntent()
		if strings.EqualFold(userIntent.GetProviderType(), util.K8sProviderType) {
			return true
		}
	}
	return false
}

func sortedK8sScrapeTargets() []string {
	targets := make([]string, 0, len(k8sSupportedScrapeTargets))
	for target := range k8sSupportedScrapeTargets {
		targets = append(targets, target)
	}
	sort.Strings(targets)
	return targets
}

// ValidateOTLPAllowed fails when the document references an OTLP provider while the global
// runtime configuration that gates OTLP exporters is off.
func ValidateOTLPAllowed(
	authAPI *ybaAuthClient.AuthAPIClient,
	resolved map[string]ExporterInfo,
) {
	referencesOTLP := false
	for _, info := range resolved {
		if info.Type == util.OTLPTelemetryProviderType {
			referencesOTLP = true
			break
		}
	}
	if !referencesOTLP {
		return
	}
	r, response, err := authAPI.GetConfigurationKey(
		globalRuntimeConfigScope, allowOTLPRuntimeConfigKey).Execute()
	if err != nil {
		// Enforced server side regardless, so let the API have the final word.
		logrus.Debugf(
			"Unable to read %s: %s\n",
			allowOTLPRuntimeConfigKey,
			util.ErrorFromHTTPResponse(
				response, err, "Runtime Configuration", "Get").Error())
		return
	}
	if strings.EqualFold(strings.TrimSpace(r), "true") {
		return
	}
	logrus.Fatal(formatter.Colorize(
		fmt.Sprintf(
			"This config references an OTLP telemetry provider, which requires the global "+
				"runtime configuration %s to be true\n",
			allowOTLPRuntimeConfigKey),
		formatter.RedColor))
}

// IsSameConfig reports whether two configs describe the same export setup. The API rejects
// a no-op with a 400, so the CLI skips the call instead.
func IsSameConfig(a, b *ybav2client.TelemetryConfig) bool {
	aBytes, err := json.Marshal(a)
	if err != nil {
		return false
	}
	bBytes, err := json.Marshal(b)
	if err != nil {
		return false
	}
	return string(aBytes) == string(bBytes)
}

// DisabledSections lists export types that are exporting today but would stop under the
// requested config, since omitting a section disables it.
func DisabledSections(current, requested *ybav2client.TelemetryConfig) []string {
	requestedByName := make(map[string]Section)
	for _, section := range Sections(requested) {
		requestedByName[section.Name] = section
	}
	disabled := make([]string, 0)
	for _, section := range Sections(current) {
		if section.IsExporting() && !requestedByName[section.Name].IsExporting() {
			disabled = append(disabled, section.Name)
		}
	}
	return disabled
}

// Validations resolves the universe and enforces the version gate on the unified API.
func Validations(cmd *cobra.Command) (
	*ybaAuthClient.AuthAPIClient,
	ybaclient.UniverseResp,
	error,
) {
	authAPI, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
	if err != nil {
		return nil, ybaclient.UniverseResp{}, err
	}
	allowed, version, err := authAPI.ExportTelemetryConfigYBAVersionCheck()
	if err != nil {
		return nil, ybaclient.UniverseResp{}, err
	}
	if !allowed {
		return nil, ybaclient.UniverseResp{}, fmt.Errorf(
			"telemetry export configuration is not supported by YBA version %s. "+
				"It requires stable %s or preview %s",
			version,
			util.YBAAllowExportTelemetryConfigMinStableVersion,
			util.YBAAllowExportTelemetryConfigMinPreviewVersion)
	}
	return authAPI, universe, nil
}
