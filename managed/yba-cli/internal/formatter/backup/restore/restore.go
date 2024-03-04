/*
* Copyright (c) YugaByte, Inc.
 */

package restore

import (
	"bytes"
	"fmt"
	"os"
	"text/template"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultRestoreListing = "table {{.RestoreUUID}}\t{{.UniverseUUID}}\t{{.UniverseName}}\t{{.SourceUniverseUUID}}" +
		"\t{{.SourceUniverseName}}\t{{.BackupType}}\t{{.State}}\t{{.CreateTime}}\t{{.CompletionTime}}"

	keyspaceDetails = "table {{.SourceKeyspace}}\t{{.TargetKeyspace}}\t{{.StorageLocation}}\t{{.TableNameList}}"

	restoreUUIDHeader        = "Restore UUID"
	universeUUIDHeader       = "Universe UUID"
	universeNameHeader       = "Universe Name"
	sourceUniverseUUIDHeader = "Source Universe UUID"
	sourceUniverseNameHeader = "Source Universe Name"
	backupTypeHeader         = "Backup Type"
	stateHeader              = "State"
	createTimeHeader         = "Create Time"
	completionTimeHeader     = "Completion Time"
)

// RestoreContext for restore outputs
type RestoreContext struct {
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
func (r *RestoreContext) SetRestore(restore ybaclient.RestoreResp) {
	r.r = restore
}

type restoreContext struct {
	Restore *RestoreContext
}

// Write populates the output table to be displayed in the command line
func (r *RestoreContext) Write(index int) error {
	var err error
	rc := &restoreContext{
		Restore: &RestoreContext{},
	}
	rc.Restore.r = r.r

	// Section 1
	tmpl, err := r.startSubsection(defaultRestoreListing)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	r.Output.Write([]byte(formatter.Colorize(fmt.Sprintf("Restore Details %d", index+1), formatter.BlueColor)))
	r.Output.Write([]byte("\n"))
	if err := r.ContextFormat(tmpl, rc.Restore); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	r.PostFormat(tmpl, NewRestoreContext())
	r.Output.Write([]byte("\n"))

	// Section 2: Keyspace details subSection 1
	logrus.Debugf("Number of keyspaces: %d", len(rc.Restore.r.GetRestoreKeyspaceList()))
	r.subSection("Keyspace Details")
	for i, v := range rc.Restore.r.GetRestoreKeyspaceList() {
		restoreKeyspaceContext := *NewRestoreKeyspaceContext()
		restoreKeyspaceContext.Output = os.Stdout
		restoreKeyspaceContext.Format = NewRestoreFormat(viper.GetString("output"))
		restoreKeyspaceContext.SetRestoreKeyspace(v)
		restoreKeyspaceContext.Write(i)
		r.Output.Write([]byte("\n"))
	}

	return nil
}

func (r *RestoreContext) startSubsection(format string) (*template.Template, error) {
	r.Buffer = bytes.NewBufferString("")
	r.ContextHeader = ""
	r.Format = formatter.Format(format)
	r.PreFormat()

	return r.ParseFormat()
}

func (r *RestoreContext) subSection(name string) {
	r.Output.Write([]byte("\n\n"))
	r.Output.Write([]byte(formatter.Colorize(name, formatter.GreenColor)))
	r.Output.Write([]byte("\n"))
}

// NewRestoreContext creates a new context for rendering restore
func NewRestoreContext() *RestoreContext {
	restoreCtx := RestoreContext{}
	restoreCtx.Header = formatter.SubHeaderContext{
		"RestoreUUID":        restoreUUIDHeader,
		"UniverseUUID":       universeUUIDHeader,
		"UniverseName":       universeNameHeader,
		"SourceUniverseUUID": sourceUniverseUUIDHeader,
		"SourceUniverseName": sourceUniverseNameHeader,
		"BackupType":         backupTypeHeader,
		"State":              stateHeader,
		"CreateTime":         createTimeHeader,
		"CompletionTime":     completionTimeHeader,
	}
	return &restoreCtx
}

// RestoreUUID fetches Restore UUID
func (r *RestoreContext) RestoreUUID() string {
	return r.r.GetRestoreUUID()
}

// UniverseUUID fetches Universe UUID
func (r *RestoreContext) UniverseUUID() string {
	return r.r.GetUniverseUUID()
}

// UniverseName fetches Universe Name
func (r *RestoreContext) UniverseName() string {
	return r.r.GetTargetUniverseName()
}

// SourceUniverseUUID fetches Source Universe UUID
func (r *RestoreContext) SourceUniverseUUID() string {
	return r.r.GetSourceUniverseUUID()
}

// SourceUniverseName fetches Source Universe Name
func (r *RestoreContext) SourceUniverseName() string {
	return r.r.GetSourceUniverseName()
}

// BackupType fetches Backup Type
func (r *RestoreContext) BackupType() string {
	return r.r.GetBackupType()
}

// State fetches Restore State
func (r *RestoreContext) State() string {
	return r.r.GetState()
}

// CreateTime fetches Restore Create Time
func (r *RestoreContext) CreateTime() string {
	return r.r.GetCreateTime().Format(time.RFC1123Z)
}

// CompletionTime fetches Restore Completion Time
func (r *RestoreContext) CompletionTime() string {
	completionTime := r.r.GetUpdateTime()
	if completionTime.IsZero() {
		return ""
	} else {
		return completionTime.Format(time.RFC1123Z)
	}
}
