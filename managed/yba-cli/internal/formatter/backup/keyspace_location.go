/*
* Copyright (c) YugaByte, Inc.
 */

package backup

import (
	"bytes"
	"encoding/json"
	"fmt"
	"text/template"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultKeyspaceLocation = "table {{.Keyspace}}\t{{.DefaultLocation}}\t{{.BackupSizeInBytes}}\t{{.TableUUIDList}}\t{{.TableNameList}}"

	keyspaceHeader          = "Keyspace"
	defaultLocationHeader   = "Default Location"
	backupSizeInBytesHeader = "Backup size (Bytes)"
	tableUUIDListHeader     = "Table UUID list"
	tableNameListHeader     = "Table name list"
)

// KeyspaceLocationContext for keyspace location outputs
type KeyspaceLocationContext struct {
	formatter.HeaderContext
	formatter.Context
	k ybaclient.KeyspaceTablesList
}

// NewKeyspaceLocationFormat for formatting output
func NewKeyspaceLocationFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := defaultKeyspaceLocation
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// SetKeyspaceLocation initializes the context with the keyspace location data
func (k *KeyspaceLocationContext) SetKeyspaceLocation(keyspaceLocation ybaclient.KeyspaceTablesList) {
	k.k = keyspaceLocation
}

type keyspaceLocationContext struct {
	KeyspaceLocation *KeyspaceLocationContext
}

// Write populates the output table to be displayed in the command line
func (k *KeyspaceLocationContext) Write(index int) error {
	var err error
	kc := &keyspaceLocationContext{
		KeyspaceLocation: &KeyspaceLocationContext{},
	}
	kc.KeyspaceLocation.k = k.k

	// Section 1
	tmpl, err := k.startSubsection(defaultKeyspaceLocation)
	if err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	k.Output.Write([]byte(formatter.Colorize(fmt.Sprintf("Keyspace %d Details", index+1), formatter.BlueColor)))
	k.Output.Write([]byte("\n"))
	if err := k.ContextFormat(tmpl, kc.KeyspaceLocation); err != nil {
		logrus.Errorf("%s", err.Error())
		return err
	}
	k.PostFormat(tmpl, NewKeyspaceLocationContext())
	k.Output.Write([]byte("\n"))

	return nil
}

func (k *KeyspaceLocationContext) startSubsection(format string) (*template.Template, error) {
	k.Buffer = bytes.NewBufferString("")
	k.ContextHeader = ""
	k.Format = formatter.Format(format)
	k.PreFormat()

	return k.ParseFormat()
}

// NewKeyspaceLocationContext creates a new context for rendering keyspace location
func NewKeyspaceLocationContext() *KeyspaceLocationContext {
	keyspaceLocationCtx := KeyspaceLocationContext{}
	keyspaceLocationCtx.Header = formatter.SubHeaderContext{
		"Keyspace":          keyspaceHeader,
		"DefaultLocation":   defaultLocationHeader,
		"BackupSizeInBytes": backupSizeInBytesHeader,
		"TableUUIDList":     tableUUIDListHeader,
		"TableNameList":     tableNameListHeader,
	}
	return &keyspaceLocationCtx
}

// Keyspace fetches Keyspace
func (k *KeyspaceLocationContext) Keyspace() string {
	return k.k.GetKeyspace()
}

// DefaultLocation fetches Default Location
func (k *KeyspaceLocationContext) DefaultLocation() string {
	return k.k.GetDefaultLocation()
}

// BackupSizeInBytes fetches Backup Size in Bytes
func (k *KeyspaceLocationContext) BackupSizeInBytes() int64 {
	return k.k.GetBackupSizeInBytes()
}

// TableUUIDList fetches Table UUID List
func (k *KeyspaceLocationContext) TableUUIDList() []string {
	return k.k.GetTableUUIDList()
}

// TableNameList fetches Table Name List
func (k *KeyspaceLocationContext) TableNameList() []string {
	return k.k.GetTablesList()
}

// MarshalJSON function
func (k *KeyspaceLocationContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(k.k)
}
