/*
 * Copyright (c) YugaByte, Inc.
 */

package common

import ()

// Component interface used by all services and
// the Common class (general operations not performed
// specific to a service).
type Component interface {
	TemplateFile() string
	Name() string
	Uninstall(cleanData bool)
	Status()
	Start()
	Stop()
	Restart()
	Install()
}
