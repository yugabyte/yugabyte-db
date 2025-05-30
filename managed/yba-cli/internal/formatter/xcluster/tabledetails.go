/*
 * Copyright (c) YugaByte, Inc.
 */

package xcluster

import (
	"bytes"
	"encoding/json"
	"fmt"
	"text/template"
	"time"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (

	// TableDetail Details
	defaultTableDetail = "table {{.TableID}}\t{{.BackupUUID}}\t{{.RestoreUUID}}" +
		"\t{{.NeedBootstrap}}\t{{.Status}}"
	tableDetails2 = "table {{.BootstrapCreateTime}}\t{{.RestoreTime}}"
	tableDetails3 = "table {{.StreamID}}\t{{.IndexTable}}"
	tableDetails4 = "table {{.ReplicationSetupDone}}\t{{.ReplicationStatusError}}"

	backupUUIDHeader             = "Backup UUID"
	restoreUUIDHeader            = "Restore UUID"
	needBootstrapHeader          = "Needs Bootstrap"
	bootstrapCreateTimeHeader    = "Bootstrap Create Time"
	restoreTimeHeader            = "Restore Time"
	streamIDHeader               = "Stream ID"
	indexTableHeader             = "Is Index Table"
	replicationSetupDoneHeader   = "Is Replication Setup Done"
	replicationStatusErrorHeader = "Replication Status Error"
)

// TableDetailContext for table outputs
type TableDetailContext struct {
	formatter.HeaderContext
	formatter.Context
	t ybaclient.XClusterTableConfig
}

// NewTableDetailFormat for formatting output
func NewTableDetailFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := defaultTableDetail
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetTableDetail initializes the context with the table data
func (t *TableDetailContext) SetTableDetail(table ybaclient.XClusterTableConfig) {
	t.t = table
}

type tableContext struct {
	TableDetail *TableDetailContext
}

// Write populates the output table to be displayed in the command line
func (t *TableDetailContext) Write(index int) error {
	var err error
	rc := &tableContext{
		TableDetail: &TableDetailContext{},
	}
	rc.TableDetail.t = t.t
	// Section 1
	tmpl, err := t.startSubsection(defaultTableDetail)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	t.Output.Write([]byte(formatter.Colorize(
		fmt.Sprintf("Table %d: Details", index+1), formatter.BlueColor)))
	t.Output.Write([]byte("\n"))
	if err := t.ContextFormat(tmpl, rc.TableDetail); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	t.PostFormat(tmpl, NewTableDetailContext())
	t.Output.Write([]byte("\n"))

	tmpl, err = t.startSubsection(tableDetails2)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	t.Output.Write([]byte("\n"))
	if err := t.ContextFormat(tmpl, rc.TableDetail); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	t.PostFormat(tmpl, NewTableDetailContext())
	t.Output.Write([]byte("\n"))

	tmpl, err = t.startSubsection(tableDetails3)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	t.Output.Write([]byte("\n"))
	if err := t.ContextFormat(tmpl, rc.TableDetail); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	t.PostFormat(tmpl, NewTableDetailContext())
	t.Output.Write([]byte("\n"))

	tmpl, err = t.startSubsection(tableDetails4)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	t.Output.Write([]byte("\n"))
	if err := t.ContextFormat(tmpl, rc.TableDetail); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	t.PostFormat(tmpl, NewTableDetailContext())
	t.Output.Write([]byte("\n"))

	return nil
}

func (t *TableDetailContext) startSubsection(format string) (*template.Template, error) {
	t.Buffer = bytes.NewBufferString("")
	t.ContextHeader = ""
	t.Format = formatter.Format(format)
	t.PreFormat()

	return t.ParseFormat()
}

// NewTableDetailContext creates a new context for rendering tables
func NewTableDetailContext() *TableDetailContext {
	tableCtx := TableDetailContext{}
	tableCtx.Header = formatter.SubHeaderContext{
		"TableID":                tableUUIDHeader,
		"BackupUUID":             backupUUIDHeader,
		"RestoreUUID":            restoreUUIDHeader,
		"NeedBootstrap":          needBootstrapHeader,
		"Status":                 formatter.StatusHeader,
		"BootstrapCreateTime":    bootstrapCreateTimeHeader,
		"RestoreTime":            restoreTimeHeader,
		"StreamID":               streamIDHeader,
		"IndexTable":             indexTableHeader,
		"ReplicationSetupDone":   replicationSetupDoneHeader,
		"ReplicationStatusError": replicationStatusErrorHeader,
	}
	return &tableCtx
}

// TableID function
func (t *TableDetailContext) TableID() string {
	return t.t.GetTableId()
}

// BackupUUID function
func (t *TableDetailContext) BackupUUID() string {
	return t.t.GetBackupUuid()
}

// RestoreUUID function
func (t *TableDetailContext) RestoreUUID() string {
	return t.t.GetRestoreUuid()
}

// NeedBootstrap function
func (t *TableDetailContext) NeedBootstrap() string {
	return fmt.Sprintf("%t", t.t.GetNeedBootstrap())
}

// Status function
func (t *TableDetailContext) Status() string {
	return t.t.GetStatus()
}

// BootstrapCreateTime function
func (t *TableDetailContext) BootstrapCreateTime() string {
	return t.t.GetBootstrapCreateTime().Format(time.RFC1123Z)
}

// RestoreTime function
func (t *TableDetailContext) RestoreTime() string {
	return t.t.GetRestoreTime().Format(time.RFC1123Z)
}

// StreamID function
func (t *TableDetailContext) StreamID() string {
	return t.t.GetStreamId()
}

// IndexTable function
func (t *TableDetailContext) IndexTable() string {
	return fmt.Sprintf("%t", t.t.GetIndexTable())
}

// ReplicationSetupDone function
func (t *TableDetailContext) ReplicationSetupDone() string {
	return fmt.Sprintf("%t", t.t.GetReplicationSetupDone())
}

// ReplicationStatusError function
func (t *TableDetailContext) ReplicationStatusError() string {
	error := ""
	for i, v := range t.t.GetReplicationStatusErrors() {
		if i == 0 {
			error = v
		} else {
			error = fmt.Sprintf("%s, %s", error, v)
		}
	}
	return error
}

// MarshalJSON function
func (t *TableDetailContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(t.t)
}
