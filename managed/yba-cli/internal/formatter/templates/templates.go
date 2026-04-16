// Use of this source code is governed by an Apache 2.0-style
// license that can be found in the LICENSE file.

package templates

import (
	"bytes"
	"encoding/json"
	"strings"
	"text/template"

	"github.com/Masterminds/sprig/v3"
)

// basicFunctions are the set of initial
// functions provided to every template.
var basicFunctions = template.FuncMap{
	"json": func(v interface{}) string {
		buf := &bytes.Buffer{}
		enc := json.NewEncoder(buf)
		enc.SetEscapeHTML(false)
		enc.Encode(v)
		// Remove the trailing new line added by the encoder
		return strings.TrimSpace(buf.String())
	},
	"split": strings.Split,
	"join":  strings.Join,
	// nolint:staticcheck
	// strings.Title is deprecated, but we only use it for ASCII, so replacing with golang.org/x/text is out of scope
	"title":    strings.Title,
	"lower":    strings.ToLower,
	"upper":    strings.ToUpper,
	"pad":      padWithSpace,
	"truncate": truncateWithLength,
}

// HeaderFunctions are used to created headers of a table.
// This is a replacement of basicFunctions for header generation
// because we want the header to remain intact.
// Some functions like `pad` are not overridden (to preserve alignment
// with the columns).
var HeaderFunctions = template.FuncMap{
	"json": func(v string) string {
		return v
	},
	"split": func(v string, _ string) string {
		// we want the table header to show the name of the column, and not
		// split the table header itself. Using a different signature
		// here, and return a string instead of []string
		return v
	},
	"join": func(v string, _ string) string {
		// table headers are always a string, so use a different signature
		// for the "join" function (string instead of []string)
		return v
	},
	"title": func(v string) string {
		return v
	},
	"lower": func(v string) string {
		return v
	},
	"upper": func(v string) string {
		return v
	},
	"truncate": func(v string, _ int) string {
		return v
	},
}

// Parse creates a new anonymous template with the basic functions
// and parses the given format.
func Parse(format string) (*template.Template, error) {
	return NewParse("", format)
}

// New creates a new empty template with the provided tag and built-in
// template functions.
func New(tag string) *template.Template {
	return template.New(tag).Funcs(basicFunctions).Funcs(sprig.GenericFuncMap())
}

// NewParse creates a new tagged template with the basic functions
// and parses the given format.
func NewParse(tag, format string) (*template.Template, error) {
	return New(tag).Parse(format)
}

// padWithSpace adds whitespace to the input if the input is non-empty
func padWithSpace(source string, prefix, suffix int) string {
	if source == "" {
		return source
	}
	return strings.Repeat(" ", prefix) + source + strings.Repeat(" ", suffix)
}

// truncateWithLength truncates the source string up to the length provided by the input
func truncateWithLength(source string, length int) string {
	if len(source) < length {
		return source
	}
	return source[:length]
}
