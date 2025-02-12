/*
 * Copyright (c) YugaByte, Inc.
 */

package destination

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultAlertDestinationListing = "table {{.Name}}\t{{.UUID}}\t{{.DefaultDestination}}"

	alertDestination1 = "table {{.Channels}}"

	defaultDestinationHeader = "Is Default Destination"

	channelsHeader = "Channels"
)

// AlertChannels hold alert channels
var AlertChannels []ybaclient.AlertChannel

// Context for alertDestination outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	a ybaclient.AlertDestination
}

// NewAlertDestinationFormat for formatting output
func NewAlertDestinationFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultAlertDestinationListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of AlertDestinations
func Write(ctx formatter.Context, alertDestinations []ybaclient.AlertDestination) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of alertDestinations into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(alertDestinations, "", "  ")
		} else {
			output, err = json.Marshal(alertDestinations)
		}

		if err != nil {
			logrus.Errorf("Error marshaling alert destinations to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, alertDestination := range alertDestinations {
			err := format(&Context{a: alertDestination})
			if err != nil {
				logrus.Debugf("Error rendering alert destination: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewAlertDestinationContext(), render)
}

// NewAlertDestinationContext creates a new context for rendering alertDestination
func NewAlertDestinationContext() *Context {
	alertDestinationCtx := Context{}
	alertDestinationCtx.Header = formatter.SubHeaderContext{
		"Name":               formatter.NameHeader,
		"UUID":               formatter.UUIDHeader,
		"DefaultDestination": defaultDestinationHeader,
		"Channels":           channelsHeader,
	}
	return &alertDestinationCtx
}

// UUID fetches AlertDestination UUID
func (c *Context) UUID() string {
	return c.a.GetUuid()
}

// Name fetches AlertDestination Name
func (c *Context) Name() string {
	return c.a.GetName()
}

// DefaultDestination fetches AlertDestination DefaultDestination
func (c *Context) DefaultDestination() string {
	return fmt.Sprintf("%t", c.a.GetDefaultDestination())
}

// Channels fetches AlertDestination Channels
func (c *Context) Channels() string {
	if AlertChannels == nil {
		return fmt.Sprintf("%v", c.a.GetChannels())
	}
	channels := "-"
	for i, v := range c.a.GetChannels() {
		for _, v2 := range AlertChannels {
			if v2.GetUuid() == v {
				if i == 0 {
					channels = fmt.Sprintf("%s(%s)", v2.GetName(), v)
				} else {
					channels = fmt.Sprintf("%s\n %s(%s)", channels, v2.GetName(), v)
				}
			}
		}
	}
	return channels
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.a)
}
