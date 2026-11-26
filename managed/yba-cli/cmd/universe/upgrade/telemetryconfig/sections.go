/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryconfig

import (
	ybav2client "github.com/yugabyte/platform-go-client/v2"
)

// Signals a section can export. The server's ProviderType carries one flag per signal and
// rejects an exporter wired to a section it cannot receive.
const (
	signalLogs    = "logs"
	signalMetrics = "metrics"
)

// sectionDef describes one export type. The API models each as a flat sibling with its own
// exporter list type, so a table of accessors is the only way to treat them uniformly.
type sectionDef struct {
	// name is the JSON field name in the telemetry config document.
	name   string
	signal string
	// supportedOnKubernetes mirrors ExportType.isSupportedOnKubernetes on the server.
	supportedOnKubernetes bool
	// serverLog sections shipped after the first three, so they carry their own minimum
	// YBA version.
	serverLog bool
	// refs returns a pointer to each exporter_uuid so they can be rewritten in place.
	refs func(*ybav2client.TelemetryConfig) (bool, []*string)
}

// Ordering matches the API model so output is stable.
var sectionDefs = []sectionDef{
	{
		name:                  "audit_logs",
		signal:                signalLogs,
		supportedOnKubernetes: true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.AuditLogs
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
	{
		name:                  "query_logs",
		signal:                signalLogs,
		supportedOnKubernetes: true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.QueryLogs
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
	{
		name:                  "metrics",
		signal:                signalMetrics,
		supportedOnKubernetes: true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.Metrics
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
	{
		name:                  "master_logs",
		signal:                signalLogs,
		supportedOnKubernetes: true,
		serverLog:             true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.MasterLogs
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
	{
		name:                  "tserver_logs",
		signal:                signalLogs,
		supportedOnKubernetes: true,
		serverLog:             true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.TserverLogs
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
	{
		name:                  "ysql_conn_mgr_logs",
		signal:                signalLogs,
		supportedOnKubernetes: true,
		serverLog:             true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.YsqlConnMgrLogs
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
	{
		// node-agent runs on the VM, not inside the pods the sidecar collector can reach.
		name:                  "node_agent_logs",
		signal:                signalLogs,
		supportedOnKubernetes: false,
		serverLog:             true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.NodeAgentLogs
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
	{
		// Node provisioning is a VM and on-prem concept.
		name:                  "ynp_logs",
		signal:                signalLogs,
		supportedOnKubernetes: false,
		serverLog:             true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.YnpLogs
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
	{
		name:                  "controller_logs",
		signal:                signalLogs,
		supportedOnKubernetes: true,
		serverLog:             true,
		refs: func(c *ybav2client.TelemetryConfig) (bool, []*string) {
			s := c.ControllerLogs
			if s == nil {
				return false, nil
			}
			refs := make([]*string, 0, len(s.Exporters))
			for i := range s.Exporters {
				refs = append(refs, &s.Exporters[i].ExporterUuid)
			}
			return true, refs
		},
	},
}

// Section is a resolved view of one export type within a config document.
type Section struct {
	Name                  string
	Signal                string
	Present               bool
	ExporterUUIDs         []string
	SupportedOnKubernetes bool
	ServerLog             bool
}

// IsExporting reports whether the section ships data anywhere. Present with an empty
// exporter list means explicitly off, which is not the same as absent.
func (s Section) IsExporting() bool {
	return s.Present && len(s.ExporterUUIDs) > 0
}

// Sections flattens a telemetry config into a per export type view.
func Sections(config *ybav2client.TelemetryConfig) []Section {
	if config == nil {
		return nil
	}
	sections := make([]Section, 0, len(sectionDefs))
	for _, def := range sectionDefs {
		present, refs := def.refs(config)
		uuids := make([]string, 0, len(refs))
		for _, ref := range refs {
			uuids = append(uuids, *ref)
		}
		sections = append(sections, Section{
			Name:                  def.name,
			Signal:                def.signal,
			Present:               present,
			ExporterUUIDs:         uuids,
			SupportedOnKubernetes: def.supportedOnKubernetes,
			ServerLog:             def.serverLog,
		})
	}
	return sections
}
