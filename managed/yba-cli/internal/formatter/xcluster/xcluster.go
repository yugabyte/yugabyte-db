/*
 * Copyright (c) YugabyteDB, Inc.
 */

package xcluster

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultXClusterListing = "table {{.Name}}\t{{.UUID}}\t{{.SourceUniverse}}" +
		"\t{{.TargetUniverse}}\t{{.Status}}"
	sourceUniverseHeader       = "Source Universe"
	targetUniverseHeader       = "Target Universe"
	modifyTimeHeader           = "Modify Time"
	tableTypeHeader            = "Table Type"
	replicationGroupNameHeader = "Replication Group Name"
	sourceActiveHeader         = "Source Active"
	targetActiveHeader         = "Target Active"
	sourceStateHeader          = "Source State"
	targetStateHeader          = "Target State"
	tablesHeader               = "Tables"
	dbsHeader                  = "Databases"
	lagHeader                  = "Lag Metric Data"
	usedForDrHeader            = "Used For Disaster Recovery"
)

// SourceUniverse variable
var SourceUniverse ybaclient.UniverseResp

// TargetUniverse variable
var TargetUniverse ybaclient.UniverseResp

// Context for xClusterConfig outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	x ybaclient.XClusterConfigGetResp
}

// NewXClusterFormat for formatting xCluster outputs
func NewXClusterFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultXClusterListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of xCluster configurations
func Write(ctx formatter.Context, xClusters []ybaclient.XClusterConfigGetResp) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of xCluster configs into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(xClusters, "", "  ")
		} else {
			output, err = json.Marshal(xClusters)
		}

		if err != nil {
			logrus.Errorf("Error marshaling xCluster configs to JSON: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, xCluster := range xClusters {
			err := format(&Context{x: xCluster})
			if err != nil {
				logrus.Debugf("Error rendering xCluster: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewXClusterContext(), render)
}

// NewXClusterContext creates a new context for rendering xCluster configurations
func NewXClusterContext() *Context {
	xClusterCtx := Context{}
	xClusterCtx.Header = formatter.SubHeaderContext{
		"UUID":                 formatter.UUIDHeader,
		"Name":                 formatter.NameHeader,
		"SourceUniverse":       sourceUniverseHeader,
		"TargetUniverse":       targetUniverseHeader,
		"CreateTime":           formatter.CreateTimeHeader,
		"ModifyTime":           modifyTimeHeader,
		"TableType":            tableTypeHeader,
		"Status":               formatter.StatusHeader,
		"Type":                 formatter.TypeHeader,
		"ReplicationGroupName": replicationGroupNameHeader,
		"SourceActive":         sourceActiveHeader,
		"SourceState":          sourceStateHeader,
		"TargetActive":         targetActiveHeader,
		"TargetState":          targetStateHeader,
		"Tables":               tablesHeader,
		"Dbs":                  dbsHeader,
		"Lag":                  lagHeader,
		"UsedForDR":            usedForDrHeader,
	}
	return &xClusterCtx
}

// UUID fetches XCluster UUID
func (c *Context) UUID() string {
	return c.x.GetUuid()
}

// Name fetches XCluster Name
func (c *Context) Name() string {
	return c.x.GetName()
}

// SourceUniverse fetches Source Universe
func (c *Context) SourceUniverse() string {
	if strings.Compare(c.x.GetSourceUniverseUUID(), SourceUniverse.GetUniverseUUID()) == 0 {
		return fmt.Sprintf("%s(%s)", SourceUniverse.GetName(), SourceUniverse.GetUniverseUUID())
	}
	return "-"
}

// TargetUniverse fetches Target Universe
func (c *Context) TargetUniverse() string {
	if strings.Compare(c.x.GetTargetUniverseUUID(), TargetUniverse.GetUniverseUUID()) == 0 {
		return fmt.Sprintf("%s(%s)", TargetUniverse.GetName(), TargetUniverse.GetUniverseUUID())
	}
	return "-"
}

// CreateTime fetches Create Time
func (c *Context) CreateTime() string {
	return util.PrintTime(c.x.GetCreateTime())
}

// ModifyTime fetches Modify Time
func (c *Context) ModifyTime() string {
	return util.PrintTime(c.x.GetModifyTime())
}

// TableType fetches Table Type
func (c *Context) TableType() string {
	return c.x.GetTableType()
}

// Status fetches Status
func (c *Context) Status() string {
	paused := c.x.GetPaused()
	if paused {
		return formatter.Colorize("Paused", formatter.YellowColor)
	}
	state := c.x.GetStatus()
	switch state {
	// Green color for "Initialized" and "Running"
	case util.InitializedXClusterState, util.RunningXClusterState:
		return formatter.Colorize(state, formatter.GreenColor)

	// Yellow color for "Updating"
	case util.UpdatingXClusterState:
		return formatter.Colorize(state, formatter.YellowColor)

	// Red color for "Deleted" and "Deletion Failed" and "Failed"
	case util.DeletedXClusterUniverseState,
		util.DeletionFailedXClusterState,
		util.FailedXClusterState:
		return formatter.Colorize(state, formatter.RedColor)

	// Default: Gray color for unknown states
	default:
		return "Unknown"
	}
}

// Type fetches Type
func (c *Context) Type() string {
	return c.x.GetType()
}

// ReplicationGroupName fetches Replication Group Name
func (c *Context) ReplicationGroupName() string {
	return c.x.GetReplicationGroupName()
}

// SourceActive fetches Source Active
func (c *Context) SourceActive() string {
	return fmt.Sprintf("%t", c.x.GetSourceActive())
}

// SourceState fetches Source State
func (c *Context) SourceState() string {
	return c.x.GetSourceUniverseState()
}

// TargetActive fetches Target Active
func (c *Context) TargetActive() string {
	return fmt.Sprintf("%t", c.x.GetTargetActive())
}

// TargetState fetches Target State
func (c *Context) TargetState() string {
	return c.x.GetTargetUniverseState()
}

// Tables fetches Tables
func (c *Context) Tables() string {
	tables := "-"
	for i, table := range c.x.GetTables() {
		if i == 0 {
			tables = table
		} else {
			tables = fmt.Sprintf("%s, %s", tables, table)
		}
	}
	return tables
}

// Dbs fetches Dbs
func (c *Context) Dbs() string {
	dbs := "-"
	for i, db := range c.x.GetDbs() {
		if i == 0 {
			dbs = db
		} else {
			dbs = fmt.Sprintf("%s, %s", dbs, db)
		}
	}
	return dbs
}

// Lag fetches Lag
func (c *Context) Lag() string {
	lag, err := json.MarshalIndent(c.x.GetLag(), "", "  ")
	if err != nil {
		return "-"
	}
	return string(lag)
}

// UsedForDR fetches Used For DR
func (c *Context) UsedForDR() string {
	return fmt.Sprintf("%t", c.x.GetUsedForDr())
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.x)
}
