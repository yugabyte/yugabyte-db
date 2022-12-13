/*
 * Copyright (c) YugaByte, Inc.
 */

package common

// Component interface used by all services and
// the Common class (general operations not performed
// specific to a service).
type Component interface {
	TemplateFile() string
	Name() string
	Uninstall(cleanData bool)
	Upgrade()
	Status() Status
	Start()
	Stop()
	Restart()
	Install()
}
