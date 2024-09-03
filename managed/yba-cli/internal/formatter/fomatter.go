/*
 * Copyright (c) YugaByte, Inc.
 */

package formatter

import (
	"bytes"
	"fmt"
	"io"
	"strings"
	"text/tabwriter"
	"text/template"

	"github.com/fatih/color"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/templates"
	"golang.org/x/exp/utf8string"
)

// Format keys used to specify certain kinds of output formats
const (
	TableFormatKey  = "table"
	RawFormatKey    = "raw"
	PrettyFormatKey = "pretty"
	JSONFormatKey   = "json"

	DefaultQuietFormat = "{{.ID}}"
	jsonFormat         = "{{json .}}"
	prettyFormat       = "{{. | toPrettyJson}}"

	// default header use accross multiple formatter
	// UniverseHeader
	UniversesHeader = "Universes"
	// DescriptionHeader
	DescriptionHeader = "Description"
	// NameHeader
	NameHeader = "Name"
	// RegionsHeader
	RegionsHeader = "Regions"
	// StateHeader
	StateHeader = "State"
	// ProviderHeader
	ProviderHeader = "Provider"
	// StorageConfigurationHeader
	StorageConfigurationHeader = "StorageConfiguration"
	// CodeHeader
	CodeHeader = "Code"
	// UUIDHeader
	UUIDHeader = "UUID"
	// StatusHeader
	StatusHeader = "Status"
	// InUseHeader
	InUseHeader = "In Use"

	// GreenColor for colored output
	GreenColor = "green"
	// RedColor for colored output
	RedColor = "red"
	// BlueColor for colored output
	BlueColor = "blue"
	// YellowColor for colored output
	YellowColor = "yellow"
)

// Format is the format string rendered using the Context
type Format string

// IsTable returns true if the format is a table-type format
func (f Format) IsTable() bool {
	return strings.HasPrefix(string(f), TableFormatKey)
}

// IsJSON returns true if the format is the json format
func (f Format) IsJSON() bool {
	return string(f) == JSONFormatKey
}

// IsPrettyJSON returns true if the format is the json format
func (f Format) IsPrettyJSON() bool {
	return string(f) == PrettyFormatKey
}

// Contains returns true if the format contains the substring
func (f Format) Contains(sub string) bool {
	return strings.Contains(string(f), sub)
}

// Context contains information required by the formatter to print the output as desired.
type Context struct {
	// Output is the output stream to which the formatted string is written.
	Output io.Writer
	// Format is used to choose raw, table or custom format for the output.
	Format Format

	// internal element
	finalFormat string
	// ContextHeader to avoid ambiguity between HeaderContext.Header and Context.Header
	ContextHeader interface{}
	// Buffer
	Buffer *bytes.Buffer
}

// PreFormat function
func (c *Context) PreFormat() {
	c.finalFormat = string(c.Format)
	// TODO: handle this in the Format type
	switch {
	case c.Format.IsTable():
		c.finalFormat = c.finalFormat[len(TableFormatKey):]
	case c.Format.IsJSON():
		c.finalFormat = jsonFormat
	case c.Format.IsPrettyJSON():
		c.finalFormat = prettyFormat
	}

	c.finalFormat = strings.Trim(c.finalFormat, " ")
	r := strings.NewReplacer(`\t`, "\t", `\n`, "\n")
	c.finalFormat = r.Replace(c.finalFormat)
}

// ParseFormat function
func (c *Context) ParseFormat() (*template.Template, error) {
	tmpl, err := templates.Parse(c.finalFormat)
	if err != nil {
		return tmpl, errors.Wrap(err, "template parsing error")
	}
	return tmpl, err
}

// PostFormat function
func (c *Context) PostFormat(tmpl *template.Template, subContext SubContext) {
	if c.Format.IsTable() {
		t := tabwriter.NewWriter(c.Output, 10, 1, 3, ' ', 0)
		Buffer := bytes.NewBufferString("")
		tmpl.Funcs(templates.HeaderFunctions).Execute(Buffer, subContext.FullHeader())
		Buffer.WriteTo(t)
		t.Write([]byte("\n"))
		c.Buffer.WriteTo(t)
		t.Flush()
	} else {
		c.Buffer.WriteTo(c.Output)
	}
}

// ContextFormat function
func (c *Context) ContextFormat(tmpl *template.Template, subContext SubContext) error {
	if err := tmpl.Execute(c.Buffer, subContext); err != nil {
		return errors.Wrap(err, "template parsing error")
	}
	if c.Format.IsTable() && c.ContextHeader != nil {
		c.ContextHeader = subContext.FullHeader()
	}
	c.Buffer.WriteString("\n")
	return nil
}

// SubFormat is a function type accepted by Write()
type SubFormat func(func(SubContext) error) error

// Write the template to the Buffer using this Context
func (c *Context) Write(sub SubContext, f SubFormat) error {
	c.Buffer = bytes.NewBufferString("")
	c.PreFormat()

	tmpl, err := c.ParseFormat()
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}

	subFormat := func(subContext SubContext) error {
		return c.ContextFormat(tmpl, subContext)
	}
	if err := f(subFormat); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}

	c.PostFormat(tmpl, sub)
	return nil
}

// Colorize the message accoring the colors var
func Colorize(message string, colors string) string {
	//If Colors is disable return the message as it is.
	if viper.GetBool("disable-color") {
		color.NoColor = true
	}
	switch colors {
	case GreenColor:
		return color.GreenString(message)
	case RedColor:
		return color.RedString(message)
	case BlueColor:
		return color.BlueString(message)
	case YellowColor:
		return color.YellowString(message)
	default:
		return message
	}
}

// Truncate the text according to the length
func Truncate(text string, lenght int) string {
	if lenght <= 0 || len(text) <= 0 {
		return ""
	}
	s := utf8string.NewString(text)
	return fmt.Sprintf("%s...", s.Slice(0, lenght))
}
