/*
 * Copyright (c) YugabyteDB, Inc.
 */

package user

import (
	"bytes"
	"encoding/json"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultFullUserGeneral = defaultUserListing
	userDetails1           = "table {{.TimeZone}}\t{{.LDAPRole}}\t{{.OidcJwtAuthToken}}"
	userDetails2           = "table {{.GroupMemberships}}"
)

// FullUserContext to render User Details output
type FullUserContext struct {
	formatter.HeaderContext
	formatter.Context
	u ybaclient.UserWithFeatures
}

// SetFullUser initializes the context with the user data
func (fu *FullUserContext) SetFullUser(user ybaclient.UserWithFeatures) {
	fu.u = user
}

// NewFullUserFormat for formatting output
func NewFullUserFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultUserListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

type fullUserContext struct {
	User *Context
}

// Write populates the output table to be displayed in the command line
func (fu *FullUserContext) Write() error {
	var err error
	fuc := &fullUserContext{
		User: &Context{},
	}
	fuc.User.u = fu.u

	// Section 1
	tmpl, err := fu.startSubsection(defaultFullUserGeneral)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.Output.Write([]byte(formatter.Colorize("General", formatter.GreenColor)))
	fu.Output.Write([]byte("\n"))
	if err := fu.ContextFormat(tmpl, fuc.User); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUserContext())
	fu.Output.Write([]byte("\n"))

	// Section 2: User Details subSection 1
	tmpl, err = fu.startSubsection(userDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.subSection("User Details")
	if err := fu.ContextFormat(tmpl, fuc.User); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUserContext())
	fu.Output.Write([]byte("\n"))

	tmpl, err = fu.startSubsection(userDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := fu.ContextFormat(tmpl, fuc.User); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	fu.PostFormat(tmpl, NewUserContext())
	fu.Output.Write([]byte("\n"))

	return nil
}

func (fu *FullUserContext) startSubsection(format string) (*template.Template, error) {
	fu.Buffer = bytes.NewBufferString("")
	fu.ContextHeader = ""
	fu.Format = formatter.Format(format)
	fu.PreFormat()

	return fu.ParseFormat()
}

func (fu *FullUserContext) subSection(name string) {
	fu.Output.Write([]byte("\n"))
	fu.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	fu.Output.Write([]byte("\n"))
}

// NewFullUserContext creates a new context for rendering user
func NewFullUserContext() *FullUserContext {
	userCtx := FullUserContext{}
	userCtx.Header = formatter.SubHeaderContext{}
	return &userCtx
}

// MarshalJSON function
func (fu *FullUserContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(fu.u)
}
