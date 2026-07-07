/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cmd

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"

	"github.com/fluxcd/pkg/tar"
	"github.com/spf13/viper"
	"golang.org/x/crypto/bcrypt"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/template"
)

type nodeExporterDirectories struct {
	SystemdFileLocation string
	ConfDir             string
	WebConfFile         string
	templateFileName    string
	BinDir              string
	LogDir              string
	HttpsCertPath       string
	HttpsKeyPath        string
}

func newNodeExporterDirectories() nodeExporterDirectories {
	certPath := common.GetSelfSignedServerCertPath()
	keyPath := common.GetSelfSignedServerKeyPath()
	return nodeExporterDirectories{
		SystemdFileLocation: common.SystemdDir + "/node-exporter.service",
		ConfDir:             common.GetSoftwareRoot() + "/node-exporter/conf",
		WebConfFile:         common.GetSoftwareRoot() + "/node-exporter/conf/web.yml",
		templateFileName:    "yba-installer-node-exporter.yml",
		BinDir:              common.GetSoftwareRoot() + "/node-exporter",
		LogDir:              common.GetBaseInstall() + "/data/logs",
		HttpsCertPath:       certPath,
		HttpsKeyPath:        keyPath,
	}
}

// NodeExporter is the co-located node_exporter service.
type NodeExporter struct {
	name    string
	version string
	nodeExporterDirectories
}

// NewNodeExporter creates a new NodeExporter service struct.
func NewNodeExporter(version string) NodeExporter {
	return NodeExporter{
		name:                    "node-exporter",
		version:                 version,
		nodeExporterDirectories: newNodeExporterDirectories(),
	}
}

func (NodeExporter) IsReplicated() bool { return false }

func (ne NodeExporter) SystemdFile() string {
	return ne.SystemdFileLocation
}

func (ne NodeExporter) TemplateFile() string {
	return ne.templateFileName
}

func (ne NodeExporter) Name() string {
	return ne.name
}

func (ne NodeExporter) Version() string {
	return ne.version
}

func (ne NodeExporter) Install() error {
	log.Info("Starting node-exporter install")
	if err := template.GenerateTemplate(ne); err != nil {
		return fmt.Errorf("failed to generate node-exporter template: %w", err)
	}
	if err := ne.FixBasicAuth(); err != nil {
		return err
	}
	if err := ne.moveAndExtractPackage(); err != nil {
		return err
	}
	if err := ne.createSymlinks(); err != nil {
		return err
	}
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		if err := common.Chown(ne.BinDir, userName, userName, true); err != nil {
			return fmt.Errorf("failed to change ownership of %s: %w", ne.BinDir, err)
		}
	}
	log.Info("Finishing node-exporter install")
	return nil
}

func (ne NodeExporter) Initialize() error {
	log.Info("Starting node-exporter initialize")
	if err := ne.Start(); err != nil {
		return err
	}
	log.Info("Finishing node-exporter initialize")
	return nil
}

func (ne NodeExporter) Start() error {
	serviceName := filepath.Base(ne.SystemdFileLocation)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to start node-exporter: %w", err)
	}
	if err := systemd.Enable(false, serviceName); err != nil {
		return fmt.Errorf("failed to start node-exporter: %w", err)
	}
	if err := systemd.Start(serviceName); err != nil {
		return fmt.Errorf("failed to start node-exporter: %w", err)
	}
	log.Debug("started node-exporter")
	return nil
}

func (ne NodeExporter) Stop() error {
	serviceName := filepath.Base(ne.SystemdFileLocation)
	status, err := ne.Status()
	if err != nil {
		return err
	}
	if status.Status != common.StatusRunning {
		log.Debug(ne.name + " is already stopped")
		return nil
	}
	if err := systemd.Stop(serviceName); err != nil {
		return fmt.Errorf("failed to stop node-exporter: %w", err)
	}
	log.Info("stopped node-exporter")
	return nil
}

func (ne NodeExporter) Restart() error {
	log.Info("Restarting node-exporter..")
	serviceName := filepath.Base(ne.SystemdFileLocation)
	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("failed to restart node-exporter: %w", err)
	}
	if err := systemd.Restart(serviceName); err != nil {
		return fmt.Errorf("failed to restart node-exporter: %w", err)
	}
	return nil
}

func (ne NodeExporter) Uninstall(removeData bool) error {
	log.Info("Uninstalling node-exporter")
	if err := ne.Stop(); err != nil {
		log.Warn("failed to stop node-exporter, continuing with uninstall: " + err.Error())
	}
	err := os.Remove(ne.SystemdFileLocation)
	if err != nil {
		pe := err.(*fs.PathError)
		if !errors.Is(pe.Err, fs.ErrNotExist) {
			log.Info(fmt.Sprintf("Error %s removing systemd service %s.",
				err.Error(), ne.SystemdFileLocation))
			return err
		}
		if err := systemd.DaemonReload(); err != nil {
			return fmt.Errorf("failed to uninstall node-exporter: %w", err)
		}
	}
	return nil
}

func (ne NodeExporter) Status() (common.Status, error) {
	status := common.Status{
		Service:    ne.Name(),
		Port:       viper.GetInt("nodeExporter.port"),
		Version:    ne.version,
		ConfigLoc:  ne.WebConfFile,
		LogFileLoc: common.GetBaseInstall() + "/data/logs/node-exporter.log",
		BinaryLoc:  ne.BinDir,
	}
	status.ServiceFileLoc = ne.SystemdFileLocation

	props, err := systemd.Show(filepath.Base(ne.SystemdFileLocation), "LoadState", "SubState",
		"ActiveState", "ActiveEnterTimestamp", "ActiveExitTimestamp")
	if err != nil {
		log.Error("Failed to get node-exporter status: " + err.Error())
		return status, err
	}
	if props["LoadState"] == "not-found" {
		status.Status = common.StatusNotInstalled
	} else if props["SubState"] == "running" {
		status.Status = common.StatusRunning
		status.Since = common.StatusSince(props["ActiveEnterTimestamp"])
	} else if props["ActiveState"] == "inactive" {
		status.Status = common.StatusStopped
		status.Since = common.StatusSince(props["ActiveExitTimestamp"])
	} else {
		status.Status = common.StatusErrored
		status.Since = common.StatusSince(props["ActiveExitTimestamp"])
	}
	return status, nil
}

func (ne NodeExporter) Upgrade() error {
	ne.nodeExporterDirectories = newNodeExporterDirectories()
	if err := template.GenerateTemplate(ne); err != nil {
		return err
	}
	if err := ne.FixBasicAuth(); err != nil {
		return err
	}
	if err := ne.moveAndExtractPackage(); err != nil {
		return err
	}
	if err := ne.createSymlinks(); err != nil {
		return err
	}
	if common.HasSudoAccess() {
		if err := common.SetSoftwarePermissions(); err != nil {
			return err
		}
	}
	return ne.Start()
}

func (ne NodeExporter) Reconfigure() error {
	log.Info("Reconfiguring node-exporter")
	if err := template.GenerateTemplate(ne); err != nil {
		return fmt.Errorf("failed to generate node-exporter config template: %w", err)
	}
	if err := ne.FixBasicAuth(); err != nil {
		return fmt.Errorf("failed to fix node-exporter basic auth: %w", err)
	}
	log.Info("node-exporter reconfigured")
	return nil
}

func (ne NodeExporter) PreUpgrade() error { return nil }

// FixBasicAuth sets auth password in node-exporter web.yml.
func (ne NodeExporter) FixBasicAuth() error {
	if viper.GetBool("nodeExporter.enableAuth") {
		log.Info("Setting node-exporter authentication.")
		authKey := fmt.Sprintf("basic_auth_users.%s", viper.GetString("nodeExporter.authUsername"))
		pwd, err := bcrypt.GenerateFromPassword(
			[]byte(viper.GetString("nodeExporter.authPassword")), 10)
		if err != nil {
			return fmt.Errorf("error generating node-exporter auth password: %w", err)
		}
		return common.SetYamlValue(ne.WebConfFile, authKey, string(pwd))
	}
	return nil
}

func (ne NodeExporter) moveAndExtractPackage() error {
	packagesPath := common.GetInstallerSoftwareDir() + "/packages"

	srcPath := fmt.Sprintf(
		"%s/third-party/node_exporter-%s.linux-amd64.tar.gz",
		common.GetInstallerSoftwareDir(), ne.version)
	dstPath := fmt.Sprintf(
		"%s/node_exporter-%s.linux-amd64.tar.gz", packagesPath, ne.version)

	common.CopyFile(srcPath, dstPath)
	rExtract, errExtract := os.Open(dstPath)
	if errExtract != nil {
		log.Fatal("Error in starting the File Extraction process. " + errExtract.Error())
	}
	defer rExtract.Close()

	extPackagePath := fmt.Sprintf(
		"%s/packages/node_exporter-%s.linux-amd64", common.GetInstallerSoftwareDir(), ne.version)
	if _, err := os.Stat(extPackagePath); err == nil {
		log.Debug(extPackagePath + " already exists, skipping re-extract.")
	} else {
		if err := tar.Untar(rExtract, packagesPath, tar.WithMaxUntarSize(-1)); err != nil {
			log.Fatal(fmt.Sprintf("failed to extract file %s, error: %s", dstPath, err.Error()))
		}
		log.Debug(dstPath + " successfully extracted.")
	}
	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		if err := common.Chown(packagesPath, userName, userName, true); err != nil {
			return fmt.Errorf("failed to change ownership of %s: %w", packagesPath, err)
		}
	}
	return nil
}

func (ne NodeExporter) createSymlinks() error {
	nePkg := fmt.Sprintf("%s/packages/node_exporter-%s.linux-amd64",
		common.GetInstallerSoftwareDir(), ne.version)

	// Ensure conf directory exists
	if err := common.MkdirAll(ne.ConfDir, common.DirMode); err != nil {
		return fmt.Errorf("failed to create %s: %w", ne.ConfDir, err)
	}

	if !common.HasSudoAccess() {
		binDir := ne.BinDir
		if err := common.MkdirAll(binDir, common.DirMode); err != nil {
			return fmt.Errorf("failed to create %s: %w", binDir, err)
		}
	}

	// node_exporter tarball typically has a single binary
	binary := "node_exporter"
	src := fmt.Sprintf("%s/%s", nePkg, binary)
	dst := fmt.Sprintf("%s/%s", ne.BinDir, binary)

	// Only symlink if the source exists (the tarball structure may vary)
	if _, err := os.Stat(src); err == nil {
		if err := common.Symlink(src, dst); err != nil {
			return fmt.Errorf("failed to create symlink for %s: %w", binary, err)
		}
	} else {
		// If node_exporter binary is directly in nePkg, search for it
		entries, _ := os.ReadDir(nePkg)
		for _, e := range entries {
			if strings.HasPrefix(e.Name(), "node_exporter") && !e.IsDir() {
				if err := common.Symlink(filepath.Join(nePkg, e.Name()), dst); err != nil {
					return fmt.Errorf("failed to create symlink for %s: %w", e.Name(), err)
				}
				break
			}
		}
	}

	if common.HasSudoAccess() {
		userName := viper.GetString("service_username")
		if err := common.Chown(ne.BinDir, userName, userName, true); err != nil {
			return fmt.Errorf("failed to change ownership of %s: %w", ne.BinDir, err)
		}
	}
	return nil
}
