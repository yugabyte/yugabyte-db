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

func (e *Executor) RegisterCommandFactory(name string, factory config.CommandFactory) {
	e.commands[name] = factory
}

func (e *Executor) Exec(ctx context.Context) error {
	factory, ok := e.commands[e.args.Command]
	if !ok {
		return fmt.Errorf("Unsupported command: %s", e.args.Command)
	}
	command := factory(ctx, e.iniConfig, e.args)
	err := command.Init()
	if err != nil {
		return fmt.Errorf("Failed to initialize %s command: %v", e.args.Command, err)
	}
	// Need to validate only in case of onprem manual.
	if !config.GetBool(e.iniConfig.DefaultSectionValue(), "is_cloud", false) {
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
	return command.Execute()
}
