/*
 * Copyright (c) YugabyteDB, Inc.
 */

package perfadvisor

import (
	"encoding/json"
	"strconv"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultCollectorListing = "table {{.UUID}}\t{{.PaURL}}\t{{.Embedded}}\t{{.InUseStatus}}"
	collector1              = "table {{.YbaURL}}\t{{.MetricsURL}}\t{{.ScrapePeriod}}"

	paURLHeader        = "Perf Advisor URL"
	ybaURLHeader       = "YBA URL"
	metricsURLHeader   = "Metrics URL"
	scrapePeriodHeader = "Scrape Period (s)"
	embeddedHeader     = "Embedded"
	inUseStatusHeader  = "In Use"
)

// CollectorContext for Perf Advisor collector outputs
type CollectorContext struct {
	formatter.HeaderContext
	formatter.Context
	c ybaclient.PACollectorDetailsModel
}

// NewCollectorFormat for formatting output
func NewCollectorFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(defaultCollectorListing)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetCollector initializes the context with the collector data
func (cc *CollectorContext) SetCollector(collector ybaclient.PACollectorDetailsModel) {
	cc.c = collector
}

// WriteCollectors renders a list of Perf Advisor collectors
func WriteCollectors(
	ctx formatter.Context, collectors []ybaclient.PACollectorDetailsModel,
) error {
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		var output []byte
		var err error
		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(collectors, "", "  ")
		} else {
			output, err = json.Marshal(collectors)
		}
		if err != nil {
			logrus.Errorf("Error marshaling Perf Advisor collectors to json: %v\n", err)
			return err
		}
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, collector := range collectors {
			if err := format(&CollectorContext{c: collector}); err != nil {
				logrus.Debugf("Error rendering Perf Advisor collector: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewCollectorContext(), render)
}

// NewCollectorContext creates a new context for rendering a collector
func NewCollectorContext() *CollectorContext {
	collectorCtx := CollectorContext{}
	collectorCtx.Header = formatter.SubHeaderContext{
		"UUID":         formatter.UUIDHeader,
		"PaURL":        paURLHeader,
		"YbaURL":       ybaURLHeader,
		"MetricsURL":   metricsURLHeader,
		"ScrapePeriod": scrapePeriodHeader,
		"Embedded":     embeddedHeader,
		"InUseStatus":  inUseStatusHeader,
	}
	return &collectorCtx
}

// UUID returns the collector UUID
func (cc *CollectorContext) UUID() string {
	return cc.c.GetUuid()
}

// PaURL returns the URL of the Perf Advisor the collector runs against
func (cc *CollectorContext) PaURL() string {
	return cc.c.GetPaUrl()
}

// YbaURL returns the URL the collector uses to reach YBA
func (cc *CollectorContext) YbaURL() string {
	return cc.c.GetYbaUrl()
}

// MetricsURL returns the URL of the Prometheus the collector scrapes
func (cc *CollectorContext) MetricsURL() string {
	return cc.c.GetMetricsUrl()
}

// ScrapePeriod returns the scrape interval in seconds
func (cc *CollectorContext) ScrapePeriod() string {
	return strconv.FormatInt(cc.c.GetMetricsScrapePeriodSecs(), 10)
}

// Embedded reports whether this is the Perf Advisor YBA manages itself
func (cc *CollectorContext) Embedded() string {
	return strconv.FormatBool(cc.c.GetEmbedded())
}

// InUseStatus reports whether any universe is registered with this collector
func (cc *CollectorContext) InUseStatus() string {
	return orDash(cc.c.GetInUseStatus())
}

// MarshalJSON renders the collector as JSON
func (cc *CollectorContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(cc.c)
}
