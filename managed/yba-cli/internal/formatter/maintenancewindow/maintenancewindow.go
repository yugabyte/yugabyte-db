/*
 * Copyright (c) YugaByte, Inc.
 */

package maintenancewindow

import (
	"encoding/json"
	"fmt"
	"time"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// Universes is a slice of UniverseResp
var Universes []ybaclient.UniverseResp

const (
	defaultMaintenanceWindowListing = "table {{.Name}}\t{{.UUID}}\t{{.StartTime}}\t{{.EndTime}}\t{{.State}}"

	maintenanceWindowListing1 = "table {{.CreateTime}}\t{{.Description}}"

	maintenanceWindowListing2 = "table {{.SuppressAlertsForUniverses}}"

	maintenanceWindowListing3 = "table {{.SuppressHealthCheckForUniverses}}"

	startTimeHeader                       = "Start Time"
	endTimeHeader                         = "End Time"
	alertConfigurationAPIFilterHeader     = "Alert Configuration API Filter"
	suppressHealthCheckForUniversesHeader = "Suppress Health Check For Universes"
	suppressAlertsForUniversesHeader      = "Suppress Alerts For Universes"
)

// Context for maintenanceWindow outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	mw ybaclient.MaintenanceWindow
}

// NewMaintenanceWindowFormat for formatting output
func NewMaintenanceWindowFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultMaintenanceWindowListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for mw list of MaintenanceWindows
func Write(ctx formatter.Context, maintenanceWindows []ybaclient.MaintenanceWindow) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of maintenanceWindows into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(maintenanceWindows, "", "  ")
		} else {
			output, err = json.Marshal(maintenanceWindows)
		}

		if err != nil {
			logrus.Errorf("Error marshaling maintenanceWindows to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, maintenanceWindow := range maintenanceWindows {
			err := format(&Context{mw: maintenanceWindow})
			if err != nil {
				logrus.Debugf("Error rendering maintenanceWindow: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewMaintenanceWindowContext(), render)
}

// NewMaintenanceWindowContext creates mw new context for rendering maintenanceWindow
func NewMaintenanceWindowContext() *Context {
	maintenanceWindowCtx := Context{}
	maintenanceWindowCtx.Header = formatter.SubHeaderContext{
		"Name":                            formatter.NameHeader,
		"UUID":                            formatter.UUIDHeader,
		"State":                           formatter.StateHeader,
		"CreateTime":                      formatter.CreateTimeHeader,
		"StartTime":                       startTimeHeader,
		"EndTime":                         endTimeHeader,
		"Description":                     formatter.DescriptionHeader,
		"SuppressAlertsForUniverses":      suppressAlertsForUniversesHeader,
		"SuppressHealthCheckForUniverses": suppressHealthCheckForUniversesHeader,
	}
	return &maintenanceWindowCtx
}

// UUID fetches MaintenanceWindow UUID
func (c *Context) UUID() string {
	return c.mw.GetUuid()
}

// Name fetches MaintenanceWindow Name
func (c *Context) Name() string {
	return c.mw.GetName()
}

// State fetches the maintenanceWindow usability state
func (c *Context) State() string {
	return c.mw.GetState()
}

// CreateTime fetches the maintenanceWindow creation time
func (c *Context) CreateTime() string {
	return c.mw.GetCreateTime().Format(time.RFC1123Z)
}

// StartTime fetches the maintenanceWindow start time
func (c *Context) StartTime() string {
	return c.mw.GetStartTime().Format(time.RFC1123Z)
}

// EndTime fetches the maintenanceWindow end time
func (c *Context) EndTime() string {
	return c.mw.GetEndTime().Format(time.RFC1123Z)
}

// Description fetches the maintenanceWindow description
func (c *Context) Description() string {
	return c.mw.GetDescription()
}

// SuppressAlertsForUniverses fetches the maintenanceWindow suppress alerts for universes
func (c *Context) SuppressAlertsForUniverses() string {
	suppress := c.mw.GetAlertConfigurationFilter()
	target := suppress.GetTarget()
	if target.GetAll() {
		return "Suppressed for All Universes"
	}
	return populateUniverseUUIDs(target.GetUuids())
}

// SuppressHealthCheckForUniverses fetches the maintenanceWindow suppress health check for universes
func (c *Context) SuppressHealthCheckForUniverses() string {
	if c.mw.SuppressHealthCheckNotificationsConfig == nil {
		return "No suppress health check configuration"
	}
	suppress := c.mw.GetSuppressHealthCheckNotificationsConfig()
	if suppress.GetSuppressAllUniverses() {
		return "Suppressed for All Universes"
	}
	return populateUniverseUUIDs(suppress.GetUniverseUUIDSet())
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.mw)
}

func populateUniverseUUIDs(universeUUIDsList []string) string {
	universeUUIDs := "-"

	for i, universeUUID := range universeUUIDsList {
		uuidFound := false
		for _, v := range Universes {
			if universeUUID == v.GetUniverseUUID() {
				uuidFound = true
				if i == 0 {
					universeUUIDs = fmt.Sprintf("%s(%s)", v.GetName(), universeUUID)
				} else {
					universeUUIDs = fmt.Sprintf("%s\n%s(%s)", universeUUIDs, v.GetName(), universeUUID)
				}
			}

		}
		if !uuidFound {
			if i == 0 {
				universeUUIDs = universeUUID
			} else {
				universeUUIDs = fmt.Sprintf("%s\n%s", universeUUIDs, universeUUID)
			}
		}
	}
	return universeUUIDs
}
