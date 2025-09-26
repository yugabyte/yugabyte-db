/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundle

import (
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// Universe for support bundle
var Universe ybaclient.UniverseResp

const (
	defaultSupportBundleListing = "table {{.UUID}}\t{{.StartDate}}\t{{.EndDate}}\t{{.SizeInBytes}}\t{{.Status}}"

	supportBundle1 = "table {{.Scope}}\t{{.CreationDate}}\t{{.ExpirationDate}}"
	supportBundle2 = "table {{.Path}}"

	details1 = "table {{.Components}}"
	details2 = "table {{.MaxCoreFileSize}}\t{{.MaxNumRecentCores}}"
	details3 = "table {{.PromDumpStartDate}}\t{{.PromDumpEndDate}}"
	details4 = "table {{.PrometheusMetricsTypes}}"

	startDateHeader         = "Start Date"
	endDateHeader           = "End Date"
	sizeInBytesHeader       = "Size"
	componentsHeader        = "Components"
	maxCoreFileSizeHeader   = "Max Core File Size"
	maxNumRecentCoresHeader = "Max Number of Recent Cores"
	promDumpStartDateHeader = "Prometheus Dump Start Date"
	promDumpEndDateHeader   = "Prometheus Dump End Date"
	promDumpTypesHeader     = "Prometheus Metrics Types"
	scopeHeader             = "Scope"
	pathHeader              = "Path in YugabyteDB Anywhere"
	creationDateHeader      = "Creation Date"
	expirationDateHeader    = "Expiration Date"
)

// Context for supportBundle outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	s ybaclient.SupportBundle
}

// NewSupportBundleFormat for formatting output
func NewSupportBundleFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultSupportBundleListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of SupportBundles
func Write(ctx formatter.Context, supportBundles []ybaclient.SupportBundle) error {
	// Check if the format is JSON or Pretty JSON
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		// Marshal the slice of supportBundles into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(supportBundles, "", "  ")
		} else {
			output, err = json.Marshal(supportBundles)
		}

		if err != nil {
			logrus.Errorf("Error marshaling support bundles to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}

	// Existing logic for table and other formats
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, supportBundle := range supportBundles {
			err := format(&Context{s: supportBundle})
			if err != nil {
				logrus.Debugf("Error rendering supportBundle: %v\n", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewSupportBundleContext(), render)
}

// NewSupportBundleContext creates a new context for rendering supportBundle
func NewSupportBundleContext() *Context {
	supportBundleCtx := Context{}
	supportBundleCtx.Header = formatter.SubHeaderContext{
		"UUID":                   formatter.UUIDHeader,
		"StartDate":              startDateHeader,
		"EndDate":                endDateHeader,
		"SizeInBytes":            sizeInBytesHeader,
		"Status":                 formatter.StatusHeader,
		"Components":             componentsHeader,
		"MaxCoreFileSize":        maxCoreFileSizeHeader,
		"MaxNumRecentCores":      maxNumRecentCoresHeader,
		"PromDumpStartDate":      promDumpStartDateHeader,
		"PromDumpEndDate":        promDumpEndDateHeader,
		"PrometheusMetricsTypes": promDumpTypesHeader,
		"Scope":                  scopeHeader,
		"CreationDate":           creationDateHeader,
		"ExpirationDate":         expirationDateHeader,
		"Path":                   pathHeader,
	}
	return &supportBundleCtx
}

// UUID fetches SupportBundle UUID
func (c *Context) UUID() string {
	return c.s.GetBundleUUID()
}

// StartDate fetches SupportBundle start date
func (c *Context) StartDate() string {
	return util.PrintTime(c.s.GetStartDate())
}

// EndDate fetches SupportBundle end date
func (c *Context) EndDate() string {
	return util.PrintTime(c.s.GetEndDate())
}

// SizeInBytes fetches SupportBundle size in bytes
func (c *Context) SizeInBytes() string {
	size, unit := util.HumanReadableSize(float64(c.s.GetSizeInBytes()))
	return fmt.Sprintf("%0.2f %s", size, unit)
}

// Status fetches SupportBundle status
func (c *Context) Status() string {
	state := c.s.GetStatus()
	if strings.EqualFold(state, util.SuccessSupportBundleState) {
		return formatter.Colorize(state, formatter.GreenColor)
	} else if strings.EqualFold(state, util.FailedSupportBundleState) ||
		strings.EqualFold(state, util.AbortedSupportBundleState) {
		return formatter.Colorize(state, formatter.RedColor)
	}
	return formatter.Colorize(state, formatter.YellowColor)
}

// Scope fetches SupportBundle scope
func (c *Context) Scope() string {
	if Universe.GetUniverseUUID() == "" {
		return c.s.GetScopeUUID()
	} else if c.s.GetScopeUUID() != Universe.GetUniverseUUID() {
		return c.s.GetBundleUUID()
	}
	return fmt.Sprintf("%s(%s)", Universe.GetName(), c.s.GetScopeUUID())

}

// Path fetches SupportBundle path
func (c *Context) Path() string {
	return c.s.GetPath()
}

// CreationDate fetches SupportBundle creation date
func (c *Context) CreationDate() string {
	return util.PrintTime(c.s.GetCreationDate())
}

// ExpirationDate fetches SupportBundle expiration date
func (c *Context) ExpirationDate() string {
	return util.PrintTime(c.s.GetExpirationDate())
}

// Components fetches SupportBundle components
func (c *Context) Components() string {
	details := c.s.GetBundleDetails()
	components := ""
	for i, c := range details.GetComponents() {
		if i == 0 {
			components = c
		} else {
			components = fmt.Sprintf("%s, %s", components, c)
		}
	}
	if len(components) > 0 {
		return components
	}
	return "-"
}

// MaxCoreFileSize fetches SupportBundle max core file size
func (c *Context) MaxCoreFileSize() string {
	details := c.s.GetBundleDetails()
	size, unit := util.HumanReadableSize(float64(details.GetMaxCoreFileSize()))
	return fmt.Sprintf("%0.2f %s", size, unit)
}

// MaxNumRecentCores fetches SupportBundle max number of recent cores
func (c *Context) MaxNumRecentCores() string {
	details := c.s.GetBundleDetails()
	return fmt.Sprintf("%d", details.GetMaxNumRecentCores())
}

// PromDumpEndDate fetches SupportBundle prometheus dump end date
func (c *Context) PromDumpEndDate() string {
	details := c.s.GetBundleDetails()

	return util.PrintTime(details.GetPromDumpEndDate())
}

// PromDumpStartDate fetches SupportBundle prometheus dump start date
func (c *Context) PromDumpStartDate() string {
	details := c.s.GetBundleDetails()
	promDumpStartDate := details.GetPromDumpStartDate()
	if promDumpStartDate.IsZero() {
		return ""
	}
	return promDumpStartDate.Format(time.RFC1123Z)
}

// PrometheusMetricsTypes fetches SupportBundle prometheus metrics types
func (c *Context) PrometheusMetricsTypes() string {
	details := c.s.GetBundleDetails()
	types := ""
	for i, t := range details.GetPrometheusMetricsTypes() {
		if i == 0 {
			types = t
		} else {
			types = fmt.Sprintf("%s, %s", types, t)
		}
	}
	if len(types) > 0 {
		return types
	}
	return "-"
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.s)
}
