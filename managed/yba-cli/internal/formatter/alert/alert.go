/*
 * Copyright (c) YugaByte, Inc.
 */

package alert

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
	defaultAlertListing = "table {{.Name}}\t{{.UUID}}\t{{.Source}}\t{{.State}}\t{{.Severity}}"

	alertListing1 = "table {{.Configuration}}\t{{.DefinitionUUID}}\t{{.CreateTime}}" +
		"\t{{.AcknowledgedTime}}\t{{.ResolvedTime}}"
	alertListing3 = "table {{.Message}}"
	alertListing4 = "table {{.NextNotificationTime}}\t{{.NotificationAttemptTime}}" +
		"\t{{.NotificationsFailed}}\t{{.NotifiedState}}"
	alertListing5 = "table {{.SeverityIndex}}\t{{.StateIndex}}"

	severityHeader                = "Severity"
	sourceHeader                  = "Source"
	acknowledgedTimeHeader        = "Acknowledged Time"
	configurationHeader           = "Configuration"
	definitionUUIDHeader          = "Definition UUID"
	resolvedTimeHeader            = "Resolved Time"
	labelsHeader                  = "Labels"
	messageHeader                 = "Message"
	nextNotificationTimeHeader    = "Next Notification Time"
	notificationAttemptTimeHeader = "Notification Attempt Time"
	notificationsFailedHeader     = "Number of Notifications Failed"
	notifiedStateHeader           = "Notified State"
	severityIndexHeader           = "Severity Index"
	stateIndexHeader              = "State Index"
)

// Context for alert outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	a ybaclient.Alert
}

// NewAlertFormat for formatting output
func NewAlertFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Alerts
func Write(ctx formatter.Context, alerts []ybaclient.Alert) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of alerts into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(alerts, "", "  ")
		} else {
			output, err = json.Marshal(alerts)
		}

		if err != nil {
			logrus.Errorf("Error marshaling alerts to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, alert := range alerts {
			err := format(&Context{a: alert})
			if err != nil {
				logrus.Debugf("Error rendering alert: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewAlertContext(), render)
}

// NewAlertContext creates a new context for rendering alert
func NewAlertContext() *Context {
	alertCtx := Context{}
	alertCtx.Header = formatter.SubHeaderContext{
		"Name":                    formatter.NameHeader,
		"UUID":                    formatter.UUIDHeader,
		"State":                   formatter.StateHeader,
		"Severity":                severityHeader,
		"Source":                  sourceHeader,
		"AcknowledgedTime":        acknowledgedTimeHeader,
		"Configuration":           configurationHeader,
		"CreateTime":              formatter.CreateTimeHeader,
		"DefinitionUUID":          definitionUUIDHeader,
		"ResolvedTime":            resolvedTimeHeader,
		"Labels":                  labelsHeader,
		"Message":                 messageHeader,
		"NextNotificationTime":    nextNotificationTimeHeader,
		"NotificationAttemptTime": notificationAttemptTimeHeader,
		"NotificationsFailed":     notificationsFailedHeader,
		"NotifiedState":           notifiedStateHeader,
		"SeverityIndex":           severityIndexHeader,
		"StateIndex":              stateIndexHeader,
	}
	return &alertCtx
}

// UUID fetches Alert UUID
func (c *Context) UUID() string {
	return c.a.GetUuid()
}

// Name fetches Alert Name
func (c *Context) Name() string {
	return c.a.GetName()
}

// State fetches the alert usability state
func (c *Context) State() string {
	return c.a.GetState()
}

// Severity fetches the alert severity
func (c *Context) Severity() string {
	severity := c.a.GetSeverity()
	if strings.Compare(severity, util.SevereAlertSeverity) == 0 {
		return formatter.Colorize(severity, formatter.RedColor)
	}
	return formatter.Colorize(severity, formatter.YellowColor)
}

// Source fetches the alert source
func (c *Context) Source() string {
	return fmt.Sprintf("%s(%s)", c.a.GetSourceName(), c.a.GetSourceUUID())
}

// Configuration fetches the alert configuration
func (c *Context) Configuration() string {
	return fmt.Sprintf("%s(%s)", c.a.GetConfigurationType(), c.a.GetConfigurationUuid())
}

// AcknowledgedTime fetches the alert Acknowledged Time
func (c *Context) AcknowledgedTime() string {
	return util.PrintTime(c.a.GetAcknowledgedTime())
}

// CreateTime fetches the alert Create Time
func (c *Context) CreateTime() string {
	return util.PrintTime(c.a.GetCreateTime())
}

// DefinitionUUID fetches the alert Definition UUID
func (c *Context) DefinitionUUID() string {
	return c.a.GetDefinitionUuid()
}

// ResolvedTime fetches the alert Resolved Time
func (c *Context) ResolvedTime() string {
	return util.PrintTime(c.a.GetResolvedTime())
}

// Labels fetches the alert Labels
func (c *Context) Labels() string {
	labels := c.a.GetLabels()
	labelsString := ""
	for i, label := range labels {
		key := label.GetKey()
		if i == 0 {
			labelsString = key.GetName()
		} else {
			labelsString = fmt.Sprintf("%s, %s", labelsString, key.GetName())
		}
	}
	if len(labelsString) == 0 {
		labelsString = "-"
	}
	return labelsString
}

// Message fetches the alert Message
func (c *Context) Message() string {
	return c.a.GetMessage()
}

// NextNotificationTime fetches the alert Next Notification Time
func (c *Context) NextNotificationTime() string {
	return util.PrintTime(c.a.GetNextNotificationTime())
}

// NotificationAttemptTime fetches the alert Notification Attempt Time
func (c *Context) NotificationAttemptTime() string {
	return util.PrintTime(c.a.GetNotificationAttemptTime())
}

// NotificationsFailed fetches the alert Notifications Failed
func (c *Context) NotificationsFailed() string {
	return fmt.Sprintf("%d", c.a.GetNotificationsFailed())
}

// NotifiedState fetches the alert Notified State
func (c *Context) NotifiedState() string {
	return c.a.GetNotifiedState()
}

// SeverityIndex fetches the alert Severity Index
func (c *Context) SeverityIndex() string {
	return fmt.Sprintf("%d", c.a.GetSeverityIndex())
}

// StateIndex fetches the alert State Index
func (c *Context) StateIndex() string {
	return fmt.Sprintf("%d", c.a.GetStateIndex())
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.a)
}
