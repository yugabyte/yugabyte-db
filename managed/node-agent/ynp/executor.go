// Copyright (c) YugabyteDB, Inc.

package ynp

import (
	"context"
	"fmt"
	"node-agent/ynp/command"
	"node-agent/ynp/config"
)

type Executor struct {
	ctx       context.Context
	iniConfig *config.INIConfig
	args      config.Args
	commands  map[string]config.CommandFactory
}

func NewExecutor(iniConfig *config.INIConfig, args config.Args) *Executor {
	return &Executor{
		iniConfig: iniConfig,
		args:      args,
		commands: map[string]config.CommandFactory{
			"provision": command.NewProvisionCommand,
		},
	}
}

func (e *Executor) Exec(ctx context.Context) error {
	factory, ok := e.commands[e.args.Command]
	if !ok {
		return fmt.Errorf("unsupported command: %s", e.args.Command)
	}
	command := factory(ctx, e.iniConfig, e.args)
	// Need to validate only in case of onprem nodes.
	if config, ok := e.args.YnpConfig["extra"]; !ok || len(config) == 0 {
		if err := command.Validate(); err != nil {
			return err
		}
	}
	if e.args.ListModules {
		return command.ListModules()
	}
	if e.args.DryRun {
		return command.DryRun()
	}
	if e.args.PreflightCheck {
		return command.RunPreflightChecks()
	}
	return command.Execute(e.args.SpecificModules)
}
