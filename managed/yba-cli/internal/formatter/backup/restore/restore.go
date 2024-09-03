/*
* Copyright (c) YugaByte, Inc.
 */

package restore

import (
	"bytes"
	"encoding/json"
	"fmt"
	"strings"
	"text/template"
	"time"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup"
)

const (
	defaultRestoreListing = "table {{.RestoreUUID}}\t{{.Universe}}\t{{.SourceUniverse}}\t{{.State}}"

	restoreUUIDHeader    = "Restore UUID"
	sourceUniverseHeader = "Source Universe"
	backupTypeHeader     = "Backup Type"
	stateHeader          = "State"
	createTimeHeader     = "Create Time"
	completionTimeHeader = "Completion Time"
)

// Context for restore outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.RestoreResp
}

// NewRestoreFormat for formatting output
func NewRestoreFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultRestoreListing
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetRestore initializes the context with the restore info
func (r *Context) SetRestore(restore ybaclient.RestoreResp) {
	r.r = restore
}

type restoreContext struct {
	Restore *Context
}

// Write renders the context for a list of restores
func Write(ctx formatter.Context, restores []ybaclient.RestoreResp) error {
	// Check if the format is JSON or Pretty JSON
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		// Marshal the slice of restores into JSON
		var output []byte
		var err error

		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(restores, "", "  ")
		} else {
			output, err = json.Marshal(restores)
		}

		if err != nil {
			logrus.Errorf("Error marshaling restores to json: %v\n", err)
			return err
		}

		// Write the JSON output to the context
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, restore := range restores {
			err := format(&Context{r: restore})
			if err != nil {
				logrus.Debugf("Error rendering restore: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewRestoreContext(), render)

}

func (r *Context) startSubsection(format string) (*template.Template, error) {
	r.Buffer = bytes.NewBufferString("")
	r.ContextHeader = ""
	r.Format = formatter.Format(format)
	r.PreFormat()

	return r.ParseFormat()
}

func (r *Context) subSection(name string) {
	r.Output.Write([]byte("\n\n"))
	r.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	r.Output.Write([]byte("\n"))
}

// NewRestoreContext creates a new context for rendering restore
func NewRestoreContext() *Context {
	restoreCtx := Context{}
	restoreCtx.Header = formatter.SubHeaderContext{
		"RestoreUUID":    restoreUUIDHeader,
		"Universe":       backup.UniverseHeader,
		"SourceUniverse": sourceUniverseHeader,
		"BackupType":     backupTypeHeader,
		"State":          stateHeader,
		"CreateTime":     createTimeHeader,
		"CompletionTime": completionTimeHeader,
	}
	return &restoreCtx
}

// RestoreUUID fetches Restore UUID
func (r *Context) RestoreUUID() string {
	return r.r.GetRestoreUUID()
}

// Universe fetches Universe name and UUID
func (r *Context) Universe() string {
	if len(strings.TrimSpace(r.r.GetTargetUniverseName())) != 0 {
		return fmt.Sprintf("%s(%s)", r.r.GetTargetUniverseName(), r.r.GetUniverseUUID())
	}
	return r.r.GetUniverseUUID()
}

// SourceUniverse fetches Source Universe UUID and name
func (r *Context) SourceUniverse() string {
	if len(strings.TrimSpace(r.r.GetSourceUniverseName())) != 0 {
		return fmt.Sprintf("%s(%s)", r.r.GetSourceUniverseName(), r.r.GetSourceUniverseUUID())
	}
	return r.r.GetSourceUniverseUUID()
}

// SourceUniverseName fetches Source Universe Name
func (r *Context) SourceUniverseName() string {
	return r.r.GetSourceUniverseName()
}

// BackupType fetches Backup Type
func (r *Context) BackupType() string {
	return r.r.GetBackupType()
}

// State fetches Restore State
func (r *Context) State() string {
	state := r.r.GetState()
	if strings.Compare(state, util.CompletedRestoreState) == 0 {
		return formatter.Colorize(state, formatter.GreenColor)
	}
	if strings.Compare(state, util.FailedRestoreState) == 0 ||
		strings.Compare(state, util.AbortedRestoreState) == 0 {
		return formatter.Colorize(state, formatter.RedColor)
	}
	return formatter.Colorize(state, formatter.YellowColor)
}

// CreateTime fetches Restore Create Time
func (r *Context) CreateTime() string {
	return r.r.GetCreateTime().Format(time.RFC1123Z)
}

// CompletionTime fetches Restore Completion Time
func (r *Context) CompletionTime() string {
	completionTime := r.r.GetUpdateTime()
	if completionTime.IsZero() {
		return ""
	} else {
		return completionTime.Format(time.RFC1123Z)
	}
}
