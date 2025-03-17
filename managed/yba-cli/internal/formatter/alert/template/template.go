/*
 * Copyright (c) YugaByte, Inc.
 */

package template

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultAlertTemplateListing = "table {{.Name}}\t{{.TargetType}}\t{{.Template}}"

	alertTemplate1 = "table {{.Description}}"

	alertTemplate2 = "table {{.DurationSec}}"

	alertTemplate3 = "table {{.ThresholdReadOnly}}\t{{.ThresholdConditionReadOnly}}"

	alertTemplate4 = "table {{.ThresholdInteger}}\t{{.ThresholdMaxValue}}\t{{.ThresholdMinValue}}"

	alertTemplate5 = "table {{.ThresholdUnit}}"

	alertTemplate6 = "table {{.Thresholds}}"

	targetTypeHeader = "Target Type"

	thresholdUnitHeader = "Threshold Unit"

	thresholdsHeader = "Thresholds"

	durationSecHeader = "Duration (in seconds)"

	templateHeader = "Template"

	thresholdReadOnlyHeader = "Is Threshold Configurable" // print opp in the func

	thresholdConditionReadOnlyHeader = "Is Threshold Condition Configurable" // print opp in the func

	thresholdIntegerHeader = "Threshold Type"

	thresholdMaxValueHeader = "Maximum Value"

	thresholdMinValueHeader = "Minimum Value"
)

// Context for alertTemplate outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	a ybaclient.AlertConfigurationTemplate
}

// NewAlertTemplateFormat for formatting output
func NewAlertTemplateFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertTemplateListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of AlertTemplates
func Write(ctx formatter.Context, alertTemplates []ybaclient.AlertConfigurationTemplate) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of alertTemplates into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(alertTemplates, "", "  ")
		} else {
			output, err = json.Marshal(alertTemplates)
		}

		if err != nil {
			logrus.Errorf("Error marshaling alert templates to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, alertTemplate := range alertTemplates {
			err := format(&Context{a: alertTemplate})
			if err != nil {
				logrus.Debugf("Error rendering alert template: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewAlertTemplateContext(), render)
}

// NewAlertTemplateContext creates a new context for rendering alertTemplate
func NewAlertTemplateContext() *Context {
	alertTemplateCtx := Context{}
	alertTemplateCtx.Header = formatter.SubHeaderContext{
		"Name":                       formatter.NameHeader,
		"TargetType":                 targetTypeHeader,
		"Template":                   templateHeader,
		"Description":                formatter.DescriptionHeader,
		"DurationSec":                durationSecHeader,
		"ThresholdReadOnly":          thresholdReadOnlyHeader,
		"ThresholdConditionReadOnly": thresholdConditionReadOnlyHeader,
		"ThresholdInteger":           thresholdIntegerHeader,
		"ThresholdMaxValue":          thresholdMaxValueHeader,
		"ThresholdMinValue":          thresholdMinValueHeader,
		"ThresholdUnit":              thresholdUnitHeader,
		"Thresholds":                 thresholdsHeader,
	}
	return &alertTemplateCtx
}

// Name fetches AlertTemplate Name
func (c *Context) Name() string {
	return c.a.GetName()
}

// TargetType fetches AlertTemplate TargetType
func (c *Context) TargetType() string {
	return c.a.GetTargetType()
}

// DurationSec fetches AlertTemplate DurationSec
func (c *Context) DurationSec() string {
	return fmt.Sprintf("%d", c.a.GetDurationSec())
}

// ThresholdUnit fetches AlertTemplate ThresholdUnit
func (c *Context) ThresholdUnit() string {
	return fmt.Sprintf("(%s)%s", c.a.GetThresholdUnit(), c.a.GetThresholdUnitName())
}

// Thresholds fetches AlertTemplate Thresholds
func (c *Context) Thresholds() string {
	thresholds, err := json.MarshalIndent(c.a.GetThresholds(), "", "  ")
	if err != nil {
		return "-"
	}
	return string(thresholds)
}

// Description fetches AlertTemplate Description
func (c *Context) Description() string {
	return c.a.GetDescription()
}

// Template fetches AlertTemplate Template
func (c *Context) Template() string {
	return c.a.GetTemplate()
}

// ThresholdReadOnly fetches AlertTemplate ThresholdReadOnly
func (c *Context) ThresholdReadOnly() string {
	return fmt.Sprintf("%t", !c.a.GetThresholdReadOnly())
}

// ThresholdConditionReadOnly fetches AlertTemplate ThresholdConditionReadOnly
func (c *Context) ThresholdConditionReadOnly() string {
	return fmt.Sprintf("%t", !c.a.GetThresholdConditionReadOnly())
}

// ThresholdInteger fetches AlertTemplate ThresholdInteger
func (c *Context) ThresholdInteger() string {
	if c.a.GetThresholdInteger() {
		return "Integer"
	}
	return "Float"
}

// ThresholdMaxValue fetches AlertTemplate ThresholdMaxValue
func (c *Context) ThresholdMaxValue() string {
	return fmt.Sprintf("%0.2f", c.a.GetThresholdMaxValue())
}

// ThresholdMinValue fetches AlertTemplate ThresholdMinValue
func (c *Context) ThresholdMinValue() string {
	return fmt.Sprintf("%0.2f", c.a.GetThresholdMinValue())
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.a)
}
