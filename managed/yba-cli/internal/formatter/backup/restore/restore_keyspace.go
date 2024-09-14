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

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultRestoreKeyspace = "table {{.SourceKeyspace}}" +
		"\t{{.TargetKeyspace}}\t{{.BackupSizeFromStorageLocation}}"
	restoreKeyspaceDetails1 = "table {{.StorageLocation}}"
	restoreKeyspaceDetails2 = "table {{.TableNameList}}"

	sourceKeyspaceHeader  = "Source Keyspace"
	targetKeyspaceHeader  = "Target Keyspace"
	storageLocationHeader = "Storage Location"
	backupSizeHeader      = "Backup Size From Storage Location"
	tableNameListHeader   = "Table Name list"
)

// RestoreKeyspaceContext for keyspace location outputs
type RestoreKeyspaceContext struct {
	formatter.HeaderContext
	formatter.Context
	r ybaclient.RestoreKeyspace
}

// NewRestoreKeyspaceFormat for formatting output
func NewRestoreKeyspaceFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultRestoreKeyspace
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetRestoreKeyspace initializes the context with the restore keyspace data
func (r *RestoreKeyspaceContext) SetRestoreKeyspace(restoreKeyspace ybaclient.RestoreKeyspace) {
	r.r = restoreKeyspace
}

type restoreKeyspaceContext struct {
	RestoreKeyspace *RestoreKeyspaceContext
}

// Write populates the output table to be displayed in the command line
func (r *RestoreKeyspaceContext) Write(index int) error {
	var err error
	rc := &restoreKeyspaceContext{
		RestoreKeyspace: &RestoreKeyspaceContext{},
	}
	rc.RestoreKeyspace.r = r.r

	// Section 1
	tmpl, err := r.startSubsection(defaultRestoreKeyspace)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	r.Output.Write([]byte(formatter.Colorize(fmt.Sprintf("Keyspace %d Details", index+1), formatter.BlueColor)))
	r.Output.Write([]byte("\n"))
	if err := r.ContextFormat(tmpl, rc.RestoreKeyspace); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	r.PostFormat(tmpl, NewRestoreKeyspaceContext())
	r.Output.Write([]byte("\n"))

	tmpl, err = r.startSubsection(restoreKeyspaceDetails1)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := r.ContextFormat(tmpl, rc.RestoreKeyspace); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	r.PostFormat(tmpl, NewRestoreKeyspaceContext())
	r.Output.Write([]byte("\n"))

	tmpl, err = r.startSubsection(restoreKeyspaceDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	if err := r.ContextFormat(tmpl, rc.RestoreKeyspace); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	r.PostFormat(tmpl, NewRestoreKeyspaceContext())
	r.Output.Write([]byte("\n"))

	return nil
}

func (r *RestoreKeyspaceContext) startSubsection(format string) (*template.Template, error) {
	r.Buffer = bytes.NewBufferString("")
	r.ContextHeader = ""
	r.Format = formatter.Format(format)
	r.PreFormat()

	return r.ParseFormat()
}

// NewRestoreKeyspaceContext creates a new context for rendering keyspace location
func NewRestoreKeyspaceContext() *RestoreKeyspaceContext {
	restoreKeyspaceCtx := RestoreKeyspaceContext{}
	restoreKeyspaceCtx.Header = formatter.SubHeaderContext{
		"SourceKeyspace":                sourceKeyspaceHeader,
		"TargetKeyspace":                targetKeyspaceHeader,
		"StorageLocation":               storageLocationHeader,
		"BackupSizeFromStorageLocation": backupSizeHeader,
		"TableNameList":                 tableNameListHeader,
	}
	return &restoreKeyspaceCtx
}

// Keyspace fetches Keyspace
func (r *RestoreKeyspaceContext) SourceKeyspace() string {
	return r.r.GetSourceKeyspace()
}

// DefaultLocation fetches Default Location
func (r *RestoreKeyspaceContext) TargetKeyspace() string {
	return r.r.GetTargetKeyspace()
}

// BackupSizeInBytes fetches Backup Size in Bytes
func (r *RestoreKeyspaceContext) StorageLocation() string {
	return r.r.GetStorageLocation()
}

// TableUUIDList fetches Table UUID List
func (r *RestoreKeyspaceContext) BackupSizeFromStorageLocation() int64 {
	return r.r.GetBackupSizeFromStorageLocation()
}

// TableNameList fetches Table Name List
func (r *RestoreKeyspaceContext) TableNameList() string {
	commaSeparatedTableNames := strings.Join(r.r.GetTableNameList(), ", ")
	return commaSeparatedTableNames
}

// MarshalJSON function
func (r *RestoreKeyspaceContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(r.r)
}
