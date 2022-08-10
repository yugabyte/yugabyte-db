// Copyright (c) YugaByte, Inc.

package task

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"node-agent/model"
	"node-agent/util"
	"os"
	"os/exec"

	"github.com/olekukonko/tablewriter"
)

type shellTask struct {
	name string //Name of the task
	cmd  string
	args []string
	done bool
}

func NewShellTask(name string, cmd string, args []string) *shellTask {
	return &shellTask{name: name, cmd: cmd, args: args}
}
func (s shellTask) TaskName() string {
	return s.name
}

//Runs the Shell Task
func (s *shellTask) Process(ctx context.Context) (string, error) {
	util.FileLogger.Debugf("Starting the shell request - %s", s.name)
	shellCmd := exec.Command(s.cmd, s.args...)
	var out bytes.Buffer
	var errOut bytes.Buffer
	shellCmd.Stdout = &out
	shellCmd.Stderr = &errOut
	err := shellCmd.Run()
	s.done = true
	if err != nil {
		util.FileLogger.Errorf("Shell Run - %s task failed - %s", s.name, err.Error())
		//returns stderr and err
		return errOut.String(), err
	} else {
		util.FileLogger.Debugf("Shell Run - %s task successful", s.name)
		return out.String(), nil
	}
}

func (s shellTask) Done() bool {
	return s.done
}

//This handler follows the Handler type signature so that
//it can be passed to the task executor.
//It runs the script for pre flight checks and unmarshalls the output.
func HandlePreflightCheck(
	instanceTypeConfig model.NodeInstanceType,
) func(ctx context.Context) (any, error) {
	return func(ctx context.Context) (any, error) {
		util.FileLogger.Debug("Starting Preflight checks handler.")
		var err error
		preflightScriptPath := util.GetPreflightCheckPath()
		shellCmdTask := NewShellTask(
			"runPreflightCheckScript",
			util.DefaultShell,
			getOptions(preflightScriptPath, instanceTypeConfig),
		)
		output, err := shellCmdTask.Process(ctx)
		if err != nil {
			util.FileLogger.Errorf("Pre-flight checks processing failed - %s", err.Error())
			return nil, err
		}
		var parsedOut map[string]model.PreflightCheckVal
		err = json.Unmarshal([]byte(output), &parsedOut)
		if err != nil {
			util.FileLogger.Errorf("Pre-flight checks unmarshaling error - %s", err.Error())
			return nil, err
		}
		return parsedOut, nil
	}
}

func HandleUpgradeScript(config *util.Config, ctx context.Context, version string) error {
	util.FileLogger.Debug("Initializing the upgrade script")
	upgradeScriptTask := NewShellTask(
		"upgradeScript",
		util.DefaultShell,
		[]string{util.GetUpgradeScriptPath(), "upgrade", version},
	)
	errStr, err := upgradeScriptTask.Process(ctx)
	if err != nil {
		return errors.New(errStr)
	}
	return nil
}

//Shell task process for downloading the node-agent build package
func HandleDownloadPackageScript(config *util.Config, ctx context.Context) (string, error) {
	util.FileLogger.Debug("Initializing the download package script")
	jwtToken, err := util.GenerateJWT(config)
	if err != nil {
		util.FileLogger.Errorf("Failed to generate JWT during upgrade - %s", err.Error())
		return "", err
	}
	downloadPackageScript := NewShellTask(
		"downloadPackageScript",
		util.DefaultShell,
		[]string{
			util.GetInstallScriptPath(),
			"--type",
			"upgrade",
			"--url",
			config.GetString(util.PlatformHost) + ":" + config.GetString(util.PlatformPort),
			"--jwt",
			jwtToken,
		},
	)
	outStr, err := downloadPackageScript.Process(ctx)
	if err != nil {
		return outStr, errors.New(outStr)
	}
	//returns version of the downloaded package
	return outStr, nil
}

//Returns options for the preflight checks.
func getOptions(preflightScriptPath string, instanceType model.NodeInstanceType) []string {
	options := make([]string, 3)
	options[0] = preflightScriptPath
	options[1] = "-t"
	options[2] = "provision"
	if instanceType.Provider.AirGapInstall {
		options = append(options, "--airgap")
	}
	//To-do: Should the api return a string instead of a list?
	if data := instanceType.Provider.CustomHostCidrs; len(data) > 0 {
		options = append(options, "--yb_home_dir", data[0])
	} else {
		options = append(options, "--yb_home_dir", util.NodeHomeDirectory)
	}

	if data := instanceType.Provider.SshPort; data != 0 {
		options = append(options, "--ports_to_check", fmt.Sprint(data))
	}

	if data := instanceType.Details.VolumeDetailsList; len(data) > 0 {
		options = append(options, "--mount_points")
		mp := ""
		for i, volumeDetail := range data {
			mp += volumeDetail.MountPath
			if i < len(data)-1 {
				mp += ","
			}
		}
		options = append(options, mp)
	}
	return options
}

func OutputPreflightCheck(outputMap map[string]model.PreflightCheckVal) {
	table := tablewriter.NewWriter(os.Stdout)
	table.SetHeader([]string{"Pre-flight Check", "Result", "Error"})
	table.SetAlignment(tablewriter.ALIGN_LEFT)
	table.SetHeaderColor(
		tablewriter.Colors{},
		tablewriter.Colors{tablewriter.FgBlueColor},
		tablewriter.Colors{tablewriter.FgRedColor},
	)

	for k, v := range outputMap {
		data := []string{k, v.Value, v.Error}
		if v.Error == "none" {
			table.Rich(
				data,
				[]tablewriter.Colors{
					{},
					tablewriter.Colors{tablewriter.FgBlueColor},
					tablewriter.Colors{tablewriter.FgGreenColor},
				},
			)
		} else {
			table.Rich(data, []tablewriter.Colors{
				tablewriter.Colors{tablewriter.FgRedColor},
				tablewriter.Colors{tablewriter.FgRedColor},
				tablewriter.Colors{tablewriter.FgRedColor}})
		}
	}
	table.Render()
}
