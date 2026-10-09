/*
 * Copyright (c) YugabyteDB, Inc.
 */

package perfadvisor

import (
	"encoding/json"
	"strings"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultEndpointListing = "table {{.Name}}\t{{.Type}}\t{{.CollectionEndpoint}}\t{{.Universes}}\t{{.UUID}}"

	endpoint1 = "table {{.MetricsEndpoint}}\t{{.MetricsType}}"
	endpoint2 = "table {{.CollectionAuth}}\t{{.MetricsAuth}}"
	endpoint3 = "table {{.YbmAccountID}}\t{{.YbmProjectID}}"
	endpoint4 = "table {{.CreateTime}}\t{{.UpdateTime}}"

	collectionEndpointHeader = "Collection Endpoint"
	metricsEndpointHeader    = "Metrics Endpoint"
	metricsTypeHeader        = "Metrics Protocol"
	collectionAuthHeader     = "Collection Auth"
	metricsAuthHeader        = "Metrics Auth"
	ybmAccountIDHeader       = "YBM Account ID"
	ybmProjectIDHeader       = "YBM Project ID"
	universesHeader          = "Universes"
)

// EndpointContext for Perf Advisor endpoint outputs
type EndpointContext struct {
	formatter.HeaderContext
	formatter.Context
	pe ybav2client.PerfAdvisorEndpoint
}

// NewEndpointFormat for formatting output
func NewEndpointFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(defaultEndpointListing)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetEndpoint initializes the context with the endpoint data
func (c *EndpointContext) SetEndpoint(endpoint ybav2client.PerfAdvisorEndpoint) {
	c.pe = endpoint
}

// Write renders the context for a list of Perf Advisor endpoints
func Write(ctx formatter.Context, endpoints []ybav2client.PerfAdvisorEndpoint) error {
	if (ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON()) && ctx.Command.IsListCommand() {
		var output []byte
		var err error
		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(endpoints, "", "  ")
		} else {
			output, err = json.Marshal(endpoints)
		}
		if err != nil {
			logrus.Errorf("Error marshaling Perf Advisor endpoints to json: %v\n", err)
			return err
		}
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, endpoint := range endpoints {
			err := format(&EndpointContext{pe: endpoint})
			if err != nil {
				logrus.Debugf("Error rendering Perf Advisor endpoint: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewEndpointContext(), render)
}

// NewEndpointContext creates a new context for rendering a Perf Advisor endpoint
func NewEndpointContext() *EndpointContext {
	endpointCtx := EndpointContext{}
	endpointCtx.Header = formatter.SubHeaderContext{
		"Name":               formatter.NameHeader,
		"UUID":               formatter.UUIDHeader,
		"Type":               formatter.TypeHeader,
		"CreateTime":         formatter.CreateTimeHeader,
		"UpdateTime":         formatter.UpdateTimeHeader,
		"CollectionEndpoint": collectionEndpointHeader,
		"MetricsEndpoint":    metricsEndpointHeader,
		"MetricsType":        metricsTypeHeader,
		"CollectionAuth":     collectionAuthHeader,
		"MetricsAuth":        metricsAuthHeader,
		"YbmAccountID":       ybmAccountIDHeader,
		"YbmProjectID":       ybmProjectIDHeader,
		"Universes":          universesHeader,
	}
	return &endpointCtx
}

// UUID returns the endpoint UUID
func (c *EndpointContext) UUID() string {
	return c.pe.GetInfo().Uuid
}

// Name returns the endpoint name
func (c *EndpointContext) Name() string {
	return c.pe.GetSpec().Name
}

// Type returns the endpoint kind
func (c *EndpointContext) Type() string {
	return string(c.pe.GetSpec().Type)
}

// CollectionEndpoint returns the URL of the destination's Collection API
func (c *EndpointContext) CollectionEndpoint() string {
	return c.pe.GetSpec().CollectionEndpoint
}

// MetricsEndpoint returns the URL metrics are sent to
func (c *EndpointContext) MetricsEndpoint() string {
	return c.pe.GetSpec().MetricsEndpoint
}

// MetricsType returns the metrics protocol
func (c *EndpointContext) MetricsType() string {
	return string(c.pe.GetSpec().MetricsType)
}

// CollectionAuth summarizes the collection endpoint credentials
func (c *EndpointContext) CollectionAuth() string {
	spec := c.pe.GetSpec()
	return describeAuth(spec.CollectionAuth)
}

// MetricsAuth summarizes the metrics endpoint credentials
func (c *EndpointContext) MetricsAuth() string {
	spec := c.pe.GetSpec()
	return describeAuth(spec.MetricsAuth)
}

// YbmAccountID returns the YugabyteDB Managed account ID sent as a header
func (c *EndpointContext) YbmAccountID() string {
	spec := c.pe.GetSpec()
	return orDash(spec.GetYbmAccountId())
}

// YbmProjectID returns the YugabyteDB Managed project ID sent as a header
func (c *EndpointContext) YbmProjectID() string {
	spec := c.pe.GetSpec()
	return orDash(spec.GetYbmProjectId())
}

// Universes lists the universes currently forwarding to this endpoint
func (c *EndpointContext) Universes() string {
	info := c.pe.GetInfo()
	uuids := info.GetUniverseUuids()
	if len(uuids) == 0 {
		return "-"
	}
	return strings.Join(uuids, ", ")
}

// CreateTime returns when the endpoint was created
func (c *EndpointContext) CreateTime() string {
	info := c.pe.GetInfo()
	return util.PrintTime(info.GetCreateTime())
}

// UpdateTime returns when the endpoint was last edited
func (c *EndpointContext) UpdateTime() string {
	info := c.pe.GetInfo()
	return util.PrintTime(info.GetUpdateTime())
}

// MarshalJSON renders the endpoint as JSON
func (c *EndpointContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.pe)
}

// describeAuth renders credentials without the password, which YBA masks anyway.
func describeAuth(auth *ybav2client.PerfAdvisorEndpointAuth) string {
	if auth == nil || auth.Type == "NONE" {
		return "none"
	}
	if username := auth.GetUsername(); username != "" {
		return auth.Type + " (" + username + ")"
	}
	return auth.Type
}

func orDash(in string) string {
	if in == "" {
		return "-"
	}
	return in
}
