/*
 * Copyright (c) YugabyteDB, Inc.
 */

package pitr

import (
	"encoding/json"
	"fmt"
	"time"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultPITRListing = "table {{.UUID}}\t{{.Name}}\t{{.DbName}}\t" +
		"{{.ScheduleInterval}}\t{{.State}}\t{{.TableType}}\t{{.RetentionPeriod}}"

	pitrListing1 = "table {{.RetentionPeriod}}\t{{.MaxRecoverTime}}\t{{.MinRecoverTime}}"
	pitrListing2 = "table {{.CreateTime}}\t{{.UpdateTime}}"
	pitrListing3 = "table {{.CreatedForDR}}\t{{.UsedForXCluster}}"

	dbNameHeader           = "DB Name"
	retentionPeriodHeader  = "Retention Period (In seconds)"
	maxRecoverTimeHeader   = "Maximum Recover Time (In milliseconds)"
	minRecoverTimeHeader   = "Minimum Recover Time (In milliseconds)"
	scheduleIntervalHeader = "Schedule Interval (In seconds)"
	stateHeader            = "State"
	tableTypeHeader        = "Table Type"
	createTimeHeader       = "Create Time"
	updateTimeHeader       = "Update Time"
	createdForDrHeader     = "Created For DR"
	usedForXClusterHeader  = "Used For xCluster"
)

// Context for pitr outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	p ybaclient.PitrConfig
}

// NewPITRFormat for formatting output
func NewPITRFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultPITRListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of PITRs
func Write(ctx formatter.Context, pitrs []ybaclient.PitrConfig) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of pitrs into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(pitrs, "", "  ")
		} else {
			output, err = json.Marshal(pitrs)
		}

		if err != nil {
			logrus.Errorf("Error marshaling pitrs to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, pitr := range pitrs {
			err := format(&Context{p: pitr})
			if err != nil {
				logrus.Debugf("Error rendering pitr: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewPITRContext(), render)
}

// NewPITRContext creates a new context for rendering pitr
func NewPITRContext() *Context {
	pitrCtx := Context{}
	pitrCtx.Header = formatter.SubHeaderContext{
		"Name":             formatter.NameHeader,
		"UUID":             formatter.UUIDHeader,
		"DbName":           dbNameHeader,
		"RetentionPeriod":  retentionPeriodHeader,
		"MaxRecoverTime":   maxRecoverTimeHeader,
		"MinRecoverTime":   minRecoverTimeHeader,
		"ScheduleInterval": scheduleIntervalHeader,
		"State":            stateHeader,
		"TableType":        tableTypeHeader,
		"CreateTime":       createTimeHeader,
		"UpdateTime":       updateTimeHeader,
		"CreatedForDR":     createdForDrHeader,
		"UsedForXCluster":  usedForXClusterHeader,
	}
	return &pitrCtx
}

// UUID fetches PITR Config UUID
func (c *Context) UUID() string {
	return c.p.GetUuid()
}

// Name fetches PITR Config Name
func (c *Context) Name() string {
	return c.p.GetName()
}

// DbName fetches Database Name associated with PITR Config
func (c *Context) DbName() string {
	return c.p.GetDbName()
}

// State fetches the state of the PITR Config
func (c *Context) State() string {
	return c.p.GetState()
}

// MaxRecoverTime fetches the max recovery time in milliseconds
func (c *Context) MaxRecoverTime() string {
	return fmt.Sprintf("%d", c.p.GetMaxRecoverTimeInMillis())
}

// MinRecoverTime fetches the min recovery time in milliseconds
func (c *Context) MinRecoverTime() string {
	return fmt.Sprintf("%d", c.p.GetMinRecoverTimeInMillis())
}

// RetentionPeriod fetches the retention period in seconds
func (c *Context) RetentionPeriod() string {
	return fmt.Sprintf("%d", c.p.GetRetentionPeriod())
}

// ScheduleInterval fetches the schedule interval in seconds
func (c *Context) ScheduleInterval() string {
	return fmt.Sprintf("%d", c.p.GetScheduleInterval())
}

// TableType fetches the table type for the PITR config
func (c *Context) TableType() string {
	return c.p.GetTableType()
}

// CreateTime fetches the create time of the PITR config
func (c *Context) CreateTime() string {
	if c.p.CreateTime != nil {
		return c.p.CreateTime.Format(time.RFC3339)
	}
	return ""
}

// UpdateTime fetches the update time of the PITR config
func (c *Context) UpdateTime() string {
	if c.p.UpdateTime != nil {
		return c.p.UpdateTime.Format(time.RFC3339)
	}
	return ""
}

// CreatedForDR fetches the boolean indicating whether this was created for DR
func (c *Context) CreatedForDR() string {
	return fmt.Sprintf("%t", c.p.GetCreatedForDr())
}

// UsedForXCluster fetches the boolean indicating whether this
// PITR config is used for cross-cluster replication
func (c *Context) UsedForXCluster() string {
	return fmt.Sprintf("%t", c.p.GetUsedForXCluster())
}

// MarshalJSON function for PITRConfigContext
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.p)
}
