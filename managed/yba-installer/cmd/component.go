/*
 * Copyright (c) YugaByte, Inc.
 */

package cmd

import ()

// Component interface used by all services and
// the Common class (general operations not performed
// specific to a service).
type component interface {
	getTemplateFile() string
	Uninstall(cleanData bool)
}
