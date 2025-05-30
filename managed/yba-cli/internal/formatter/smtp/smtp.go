/*
 * Copyright (c) YugaByte, Inc.
 */

package smtp

import (
	"encoding/json"
	"fmt"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultSMTPListing = "table {{.EmailFrom}}\t{{.Server}}" +
		"\t{{.Port}}\t{{.Username}}\t{{.Password}}\t{{.UseSSL}}\t{{.UseTLS}}"

	emailFromHeader = "Email From"

	serverHeader = "Server"

	portHeader = "Port"

	usernameHeader = "Username"

	passwordHeader = "Password"

	useSSLHeader = "Use SSL for server connections"

	useTLSHeader = "Use TLS for server connections"
)

// Context for smtp outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	smtp ybaclient.SmtpData
}

// NewSMTPFormat for formatting output
func NewSMTPFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultSMTPListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for c list of SMTPs
func Write(ctx formatter.Context, smtps []ybaclient.SmtpData) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of smtps into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(smtps, "", "  ")
		} else {
			output, err = json.Marshal(smtps)
		}

		if err != nil {
			logrus.Errorf("Error marshaling smtps to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, smtp := range smtps {
			err := format(&Context{smtp: smtp})
			if err != nil {
				logrus.Debugf("Error rendering smtp: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewSMTPContext(), render)
}

// NewSMTPContext creates c new context for rendering smtp
func NewSMTPContext() *Context {
	smtpCtx := Context{}
	smtpCtx.Header = formatter.SubHeaderContext{
		"EmailFrom": emailFromHeader,
		"Server":    serverHeader,
		"Port":      portHeader,
		"Username":  usernameHeader,
		"Password":  passwordHeader,
		"UseSSL":    useSSLHeader,
		"UseTLS":    useTLSHeader,
	}
	return &smtpCtx
}

// EmailFrom function
func (c *Context) EmailFrom() string {
	return c.smtp.GetEmailFrom()
}

// Server function
func (c *Context) Server() string {
	return c.smtp.GetSmtpServer()
}

// Port function
func (c *Context) Port() string {
	return fmt.Sprintf("%d", c.smtp.GetSmtpPort())
}

// Username function
func (c *Context) Username() string {
	return c.smtp.GetSmtpUsername()
}

// Password function
func (c *Context) Password() string {
	return c.smtp.GetSmtpPassword()
}

// UseSSL function
func (c *Context) UseSSL() string {
	return fmt.Sprintf("%t", c.smtp.GetUseSSL())
}

// UseTLS function
func (c *Context) UseTLS() string {
	return fmt.Sprintf("%t", c.smtp.GetUseTLS())
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.smtp)
}
