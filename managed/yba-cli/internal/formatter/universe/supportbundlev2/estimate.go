/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"encoding/json"
	"fmt"
	"sort"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultEstimateListing = "table {{.Scope}}\t{{.Component}}\t{{.Size}}"

	estimateScopeHeader     = "Scope"
	estimateComponentHeader = "Component"
	estimateSizeHeader      = "Estimated Size"
)

// EstimateRow is one component's estimated size within one scope. The API answers with a nested
// map of scope to component to bytes, which has no stable iteration order and nothing to render a
// row from, so it is flattened here.
type EstimateRow struct {
	Scope       string `json:"scope"`
	Component   string `json:"component"`
	SizeInBytes int64  `json:"sizeInBytes"`
}

// EstimateContext for support bundle size estimate outputs
type EstimateContext struct {
	formatter.HeaderContext
	formatter.Context
	e EstimateRow
}

// NewEstimateFormat for formatting output
func NewEstimateFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(defaultEstimateListing)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// WriteEstimate renders a support bundle size estimate. JSON and pretty output carry the API
// response unchanged rather than the flattened rows, so a caller parsing the output sees the same
// shape the server returned.
func WriteEstimate(
	ctx formatter.Context,
	estimate *ybav2client.SupportBundleSizeEstimateResponse,
) error {
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		var output []byte
		var err error
		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(estimate, "", "  ")
		} else {
			output, err = json.Marshal(estimate)
		}
		if err != nil {
			logrus.Errorf("Error marshaling support bundle size estimate to json: %v\n", err)
			return err
		}
		// Terminated like every other command's json, which the shared formatter newline-ends.
		_, err = ctx.Output.Write(append(output, '\n'))
		return err
	}

	rows := FlattenEstimate(estimate)
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, row := range rows {
			if err := format(&EstimateContext{e: row}); err != nil {
				logrus.Debugf("Error rendering support bundle size estimate: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewEstimateContext(), render)
}

// FlattenEstimate turns the response's nested map into rows ordered by scope, then component, so
// the same estimate renders identically from one run to the next.
func FlattenEstimate(estimate *ybav2client.SupportBundleSizeEstimateResponse) []EstimateRow {
	if estimate == nil {
		return nil
	}
	data := estimate.GetData()
	if len(data) == 0 {
		return nil
	}

	scopes := make([]string, 0, len(data))
	for scope := range data {
		scopes = append(scopes, scope)
	}
	sort.Strings(scopes)

	rows := make([]EstimateRow, 0)
	for _, scope := range scopes {
		components := make([]string, 0, len(data[scope]))
		for component := range data[scope] {
			components = append(components, component)
		}
		sort.Strings(components)

		for _, component := range components {
			rows = append(rows, EstimateRow{
				Scope:       scope,
				Component:   component,
				SizeInBytes: data[scope][component],
			})
		}
	}
	return rows
}

// NewEstimateContext creates a new context for rendering a size estimate row
func NewEstimateContext() *EstimateContext {
	estimateCtx := EstimateContext{}
	estimateCtx.Header = formatter.SubHeaderContext{
		"Scope":     estimateScopeHeader,
		"Component": estimateComponentHeader,
		"Size":      estimateSizeHeader,
	}
	return &estimateCtx
}

// Scope fetches the node name, or YBA for global components
func (c *EstimateContext) Scope() string {
	return c.e.Scope
}

// Component fetches the component name
func (c *EstimateContext) Component() string {
	return c.e.Component
}

// Size fetches the estimated size
func (c *EstimateContext) Size() string {
	size, unit := util.HumanReadableSize(float64(c.e.SizeInBytes))
	return fmt.Sprintf("%0.2f %s", size, unit)
}

// MarshalJSON function
func (c *EstimateContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.e)
}
