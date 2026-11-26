// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"strings"
)

var registeredServices = []struct {
	unitName          string
	home              string
	needsProvisioning bool
}{
	{"yb-master.service", "master", false},
	{"yb-tserver.service", "tserver", false},
	{"yb-controller.service", "controller", false},
	{"yb-clean_cores.service", "", false},
	{"yb-clean_cores.timer", "", false},
	{"yb-zip_purge_yb_logs.service", "", false},
	{"yb-zip_purge_yb_logs.timer", "", false},
	{"yb-bind_check.service", "", false},
	{"yb-collect_metrics.service", "", false},
	{"yb-collect_metrics.timer", "", false},
	{"otel-collector.service", "", false},
	{"node_exporter.service", "", true},
}

// Paths in yb_home_dir that are expected to cleaned up on destroy.
var cleanupSubPaths = map[string]struct{}{
	".yugabytedb":                {},
	"bin":                        {},
	"master":                     {},
	"tserver":                    {},
	"controller":                 {},
	"cores":                      {},
	"releases":                   {},
	"yb-software":                {},
	"metrics":                    {},
	"otel-collector":             {},
	"yugabyte-client-tls-config": {},
	"yugabyte-tls-config":        {},
	"collect_metrics.pid":        {},
}

type DestroyServerHandler struct {
	param    *pb.DestroyServerInput
	username string
	logOut   util.Buffer
}

// NewDestroyServerHandler returns a new instance of DestroyServerHandler.
func NewDestroyServerHandler(param *pb.DestroyServerInput, username string) *DestroyServerHandler {
	return &DestroyServerHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *DestroyServerHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

// String implements the AsyncTask method.
func (h *DestroyServerHandler) String() string {
	return "runDestroyServer"
}

// cleanInstance cleans up the instance by deleting files and directories in ybHomeDir.
func (h *DestroyServerHandler) cleanInstance(ctx context.Context) error {
	failedPaths := []string{}
	deleteFn := func(path string) {
		util.FileLogger().Infof(ctx, "Cleaning path %s", path)
		h.logOut.WriteLine("Cleaning path %s", path)
		err := os.RemoveAll(path)
		if err != nil {
			h.logOut.WriteLine("Failed to clean path %s - %s", path, err.Error())
			util.FileLogger().Errorf(ctx, "Failed to clean path %s - %s", path, err.Error())
			failedPaths = append(failedPaths, path)
		}
	}
	ybHomeDir := h.param.GetYbHomeDir()
	entries, err := os.ReadDir(ybHomeDir)
	if err != nil {
		return fmt.Errorf("Error reading ybHomeDir %s - %s", ybHomeDir, err.Error())
	}
	for _, entry := range entries {
		if !h.isCleanupPath(entry.Name()) {
			util.FileLogger().Infof(ctx, "Skipping path %s", entry.Name())
			continue
		}
		path := filepath.Join(ybHomeDir, entry.Name())
		info, err := entry.Info()
		if err != nil {
			util.FileLogger().
				Warnf(ctx, "Error accessing path %s - %s. It may have been removed", path, err.Error())
			continue
		}
		// Check if it's a symlink
		if info.Mode()&os.ModeSymlink != 0 {
			realPath, err := filepath.EvalSymlinks(path)
			if err != nil {
				util.FileLogger().Warnf(ctx, "Broken symlink %s - %s", path, err.Error())
				continue
			}
			util.FileLogger().Infof(ctx, "Symlink %s is resolved to %s", path, realPath)
			// Handle the real path first.
			deleteFn(realPath)
		}
		deleteFn(path)
	}
	// Federation artifacts (creds/env) live under ~/.yugabyte, outside the standard cleanup
	// subpaths; remove them explicitly so credentials never linger on a released/reused node.
	if federationPathExists(federationDir(ybHomeDir)) {
		deleteFn(federationDir(ybHomeDir))
	}
	if len(failedPaths) > 0 {
		return fmt.Errorf(
			"Failed to clean the following paths: %s",
			strings.Join(failedPaths, ", "),
		)
	}
	return nil
}

// isCleanupPath reports whether the entry of ybHomeDir with the given name is deleted on destroy.
func (h *DestroyServerHandler) isCleanupPath(name string) bool {
	if _, ok := cleanupSubPaths[name]; ok {
		return true
	}
	// Provisioning lays out node_exporter here, and its unit was removed with the other provisioned
	// units. Without provisioning cleanup the unit stays, so its directory does too.
	return h.param.GetIsProvisioningCleanup() && name == filepath.Dir(nodeExporterWebConfigSubpath)
}

// emptyNodeExporterWebConfig empties node_exporter's web.yml, if it has content, and reports
// whether it did.
func (h *DestroyServerHandler) emptyNodeExporterWebConfig(ctx context.Context) (bool, error) {
	webConfig := filepath.Join(h.param.GetYbHomeDir(), nodeExporterWebConfigSubpath)
	info, err := os.Stat(webConfig)
	if errors.Is(err, os.ErrNotExist) || (err == nil && info.Size() == 0) {
		return false, nil
	}
	if err != nil {
		return false, err
	}
	util.FileLogger().Infof(ctx, "Emptying %s", webConfig)
	h.logOut.WriteLine("Emptying %s", webConfig)
	return true, os.Truncate(webConfig, 0)
}

// resetNodeExporterScheme puts a node_exporter that stays on the node back to plain HTTP. Its
// web.yml names the node certificate and key, which cleanInstance deletes with yugabyte-tls-config,
// and node_exporter does not start with a missing file. The universe the node joins next sets the
// scheme again.
func (h *DestroyServerHandler) resetNodeExporterScheme(ctx context.Context) error {
	if h.param.GetIsProvisioningCleanup() {
		// cleanInstance removed node_exporter's directory.
		return nil
	}
	emptied, err := h.emptyNodeExporterWebConfig(ctx)
	if err != nil || !emptied {
		return err
	}
	return module.ControlSystemdService(ctx, h.username, nodeExporterService, "restart", h.logOut)
}

func (h *DestroyServerHandler) cleanupNode(ctx context.Context) error {
	// This script is already customized with the correct mount points.
	cleanupScript := filepath.Join(h.param.GetYbHomeDir(), "bin/yb-server-ctl.sh")
	if _, err := os.Stat(cleanupScript); err == nil {
		for _, info := range registeredServices {
			if info.home == "" {
				continue
			}
			util.FileLogger().
				Infof(ctx, "Running control script to clean and clean-logs for %s", info.unitName)
			for _, action := range []string{"clean", "clean-logs"} {
				cmd := fmt.Sprintf("%s %s %s", cleanupScript, info.home, action)
				h.logOut.WriteLine("Running cleanup command %s", cmd)
				util.FileLogger().Infof(ctx, "Running command: %s", cmd)
				if _, err := module.RunShellCmd(ctx, h.username, action, cmd, h.logOut); err != nil {
					util.FileLogger().
						Errorf(ctx, "Failed to run %s command for %s - %s", cmd, info.unitName, err.Error())
					return err
				}
			}
		}
	} else if !os.IsNotExist(err) {
		return fmt.Errorf("Error accessing cleanup script %s: %s", cleanupScript, err.Error())
	} else {
		h.logOut.WriteLine("Skipping node cleanup as file %s is not found", cleanupScript)
		util.FileLogger().Warnf(ctx, "Skipping node cleanup as file %s is not found", cleanupScript)
	}
	return h.cleanInstance(ctx)
}

// Handle implements the AsyncTask method.
func (h *DestroyServerHandler) Handle(
	ctx context.Context,
) (*pb.DescribeTaskResponse, error) {
	if h.param.GetYbHomeDir() == "" {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	for _, info := range registeredServices {
		if info.needsProvisioning && !h.param.GetIsProvisioningCleanup() {
			h.logOut.WriteLine(
				"Skipping %s as it is not part of provisioning cleanup",
				info.unitName,
			)
			continue
		}
		unitPath, err := module.SystemdUnitPath(ctx, h.username, info.unitName, h.logOut)
		if err != nil {
			return nil, err
		}
		h.logOut.WriteLine("Found systemd unit path for %s: %s", info.unitName, unitPath)
		if strings.HasPrefix(unitPath, "/") {
			unitPathToBeRemoved := ""
			if h.param.GetIsProvisioningCleanup() {
				unitPathToBeRemoved = unitPath
			}
			if err := module.DisableSystemdService(ctx, h.username, info.unitName, unitPathToBeRemoved, h.logOut); err != nil {
				return nil, err
			}
			if info.home != "" {
				confFilepath := filepath.Join(h.param.GetYbHomeDir(), info.home, "conf/server.conf")
				util.FileLogger().Infof(ctx, "Removing server conf file %s", confFilepath)
				err = os.Remove(confFilepath)
				if err != nil && !os.IsNotExist(err) {
					util.FileLogger().
						Errorf(ctx, "Failed to delete conf file %s - %s", confFilepath, err.Error())
					return nil, err
				}
			}
		}
	}
	if err := h.cleanupNode(ctx); err != nil {
		return nil, err
	}
	if err := h.resetNodeExporterScheme(ctx); err != nil {
		return nil, err
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_DestroyServerOutput{
			DestroyServerOutput: &pb.DestroyServerOutput{},
		},
	}, nil
}
