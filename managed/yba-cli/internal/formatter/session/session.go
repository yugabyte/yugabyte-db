/*
 * Copyright (c) YugaByte, Inc.
 */

package session

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultSessionListing = "table {{.CustomerUUID}}\t{{.ApiToken}}"

	customerUUIDHeader = "Customer UUID"
	userUUIDHeader     = "User UUID"
	apiTokenHeader     = "API Token"
	authTokenHeader    = "Auth Token"
)

// Context for session outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	s ybaclient.SessionInfo
}

// NewSessionFormat for formatting output
func NewSessionFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultSessionListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of Sessions
func Write(ctx formatter.Context, sessions []ybaclient.SessionInfo) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of sessions into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(sessions, "", "  ")
		} else {
			output, err = json.Marshal(sessions)
		}

		if err != nil {
			logrus.Errorf("Error marshaling sessions to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, session := range sessions {
			err := format(&Context{s: session})
			if err != nil {
				logrus.Debugf("Error rendering session: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewSessionContext(), render)
}

// NewSessionContext creates a new context for rendering session
func NewSessionContext() *Context {
	sessionCtx := Context{}
	sessionCtx.Header = formatter.SubHeaderContext{
		"CustomerUUID": customerUUIDHeader,
		"UserUUID":     userUUIDHeader,
		"ApiToken":     apiTokenHeader,
		"AuthToken":    authTokenHeader,
	}
	return &sessionCtx
}

// CustomerUUID fetches Customer UUID
func (c *Context) CustomerUUID() string {
	return c.s.GetCustomerUUID()
}

// UserUUID fetches User UUID
func (c *Context) UserUUID() string {
	return c.s.GetUserUUID()
}

// ApiToken fetches api token
func (c *Context) ApiToken() string {
	return c.s.GetApiToken()
}

// AuthToken fetches auth token
func (c *Context) AuthToken() string {
	return c.s.GetAuthToken()
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.s)
}
