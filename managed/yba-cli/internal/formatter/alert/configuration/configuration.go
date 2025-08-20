/*
 * Copyright (c) YugaByte, Inc.
 */

package configuration

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultAlertConfigurationListing = "table {{.Name}}\t{{.UUID}}" +
		"\t{{.TargetType}}\t{{.Destination}}\t{{.Active}}"

	alertConfiguration1 = "table {{.Target}}"

	alertConfiguration2 = "table {{.Template}}\t{{.Description}}"

	alertConfiguration3 = "table {{.CreateTime}}\t{{.AlertCount}}" +
		"\t{{.DurationSec}}"

	alertConfiguration4 = "table {{.MaintenanceWindowUuids}}"

	alertConfiguration5 = "table {{.Labels}}"

	alertConfiguration6 = "table {{.ThresholdUnit}}"

	alertConfiguration7 = "table {{.Thresholds}}"

	targetHeader = "Target"

	targetTypeHeader = "Target Type"

	destinationHeader = "Destination"

	thresholdUnitHeader = "Threshold Unit"

	thresholdsHeader = "Thresholds"

	defaultDestinationHeader = "Is default destination used"

	durationSecHeader = "Duration (in seconds)"

	activeHeader = "Is Active"

	templateHeader = "Template"

	alertCountHeader = "Alert Count"

	maintenanceWindowUuidsHeader = "Maintenance Window UUIDs"

	labelsHeader = "Labels"
)

// AlertDestinations hold alert destinations
var AlertDestinations []ybaclient.AlertDestination

// Templates hold alert configuration templates
var Templates []ybaclient.AlertConfigurationTemplate

// Context for alertConfiguration outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	a ybaclient.AlertConfiguration
}

// NewAlertConfigurationFormat for formatting output
func NewAlertConfigurationFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertConfigurationListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of AlertConfigurations
func Write(ctx formatter.Context, alertConfigurations []ybaclient.AlertConfiguration) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of alertConfigurations into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(alertConfigurations, "", "  ")
		} else {
			output, err = json.Marshal(alertConfigurations)
		}

		if err != nil {
			logrus.Errorf("Error marshaling alert configurations to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, alertConfiguration := range alertConfigurations {
			err := format(&Context{a: alertConfiguration})
			if err != nil {
				logrus.Debugf("Error rendering alert configuration: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewAlertConfigurationContext(), render)
}

// NewAlertConfigurationContext creates a new context for rendering alertConfiguration
func NewAlertConfigurationContext() *Context {
	alertConfigurationCtx := Context{}
	alertConfigurationCtx.Header = formatter.SubHeaderContext{
		"Name":                   formatter.NameHeader,
		"UUID":                   formatter.UUIDHeader,
		"Target":                 targetHeader,
		"TargetType":             targetTypeHeader,
		"Destination":            destinationHeader,
		"Active":                 activeHeader,
		"Template":               templateHeader,
		"Description":            formatter.DescriptionHeader,
		"CreateTime":             formatter.CreateTimeHeader,
		"AlertCount":             alertCountHeader,
		"DurationSec":            durationSecHeader,
		"MaintenanceWindowUuids": maintenanceWindowUuidsHeader,
		"Labels":                 labelsHeader,
		"ThresholdUnit":          thresholdUnitHeader,
		"Thresholds":             thresholdsHeader,
	}
	return &alertConfigurationCtx
}

// UUID fetches AlertConfiguration UUID
func (c *Context) UUID() string {
	return c.a.GetUuid()
}

// Name fetches AlertConfiguration Name
func (c *Context) Name() string {
	return c.a.GetName()
}

// TargetType fetches AlertConfiguration TargetType
func (c *Context) TargetType() string {
	return c.a.GetTargetType()
}

// Destination fetches AlertConfiguration Destination
func (c *Context) Destination() string {
	if c.a.GetDefaultDestination() {
		return "Default Destination"
	}
	if AlertDestinations == nil {
		return c.a.GetDestinationUUID()
	}
	for _, v := range AlertDestinations {
		if v.GetUuid() == c.a.GetDestinationUUID() {
			return fmt.Sprintf("%s(%s)", v.GetName(), c.a.GetDestinationUUID())
		}
	}
	return c.a.GetDestinationUUID()
}

// Active fetches AlertConfiguration Active
func (c *Context) Active() string {
	return fmt.Sprintf("%t", c.a.GetActive())
}

// Template fetches AlertConfiguration Template
func (c *Context) Template() string {
	if Templates == nil {
		return c.a.GetTemplate()
	}
	for _, v := range Templates {
		if v.GetTemplate() == c.a.GetTemplate() {
			return fmt.Sprintf("%s(%s)", v.GetName(), v.GetTemplate())
		}
	}
	return c.a.GetTemplate()
}

// CreateTime fetches AlertConfiguration CreateTime
func (c *Context) CreateTime() string {
	return util.PrintTime(c.a.GetCreateTime())
}

// AlertCount fetches AlertConfiguration AlertCount
func (c *Context) AlertCount() string {
	return fmt.Sprintf("%0.2f", c.a.GetAlertCount())
}

// DurationSec fetches AlertConfiguration DurationSec
func (c *Context) DurationSec() string {
	return fmt.Sprintf("%d", c.a.GetDurationSec())
}

// MaintenanceWindowUuids fetches AlertConfiguration MaintenanceWindowUuids
func (c *Context) MaintenanceWindowUuids() string {
	uuids := "-"
	for i, v := range c.a.GetMaintenanceWindowUuids() {
		if i == 0 {
			uuids = v
		} else {
			uuids = fmt.Sprintf("%s, %s", uuids, v)
		}
	}
	return uuids
}

// Target fetches AlertConfiguration Target
func (c *Context) Target() string {
	targetConfig := c.a.GetTarget()
	target := "-"

	if len(targetConfig.GetUuids()) == 0 {
		return "All targets of target type " + c.a.GetTargetType()
	}

	for i, v := range targetConfig.GetUuids() {
		if i == 0 {
			target = v
		} else {
			target = fmt.Sprintf("%s, %s", target, v)
		}
	}
	return target
}

// Labels fetches AlertConfiguration Labels
func (c *Context) Labels() string {
	labels, err := json.MarshalIndent(c.a.GetLabels(), "", "  ")
	if err != nil {
		return "-"
	}
	return string(labels)
}

// ThresholdUnit fetches AlertConfiguration ThresholdUnit
func (c *Context) ThresholdUnit() string {
	return c.a.GetThresholdUnit()
}

// Thresholds fetches AlertConfiguration Thresholds
func (c *Context) Thresholds() string {
	thresholds, err := json.MarshalIndent(c.a.GetThresholds(), "", "  ")
	if err != nil {
		return "-"
	}
	return string(thresholds)
}

// Description fetches AlertConfiguration Description
func (c *Context) Description() string {
	return c.a.GetDescription()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.a)
}
