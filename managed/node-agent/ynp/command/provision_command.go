// Copyright (c) YugabyteDB, Inc.

package command

import (
	"context"
	"fmt"
	"node-agent/util"
	"node-agent/ynp/config"
	backuputils "node-agent/ynp/module/provision/backup_utils"
	"node-agent/ynp/module/provision/chrony"
	"node-agent/ynp/module/provision/clockbound"
	"node-agent/ynp/module/provision/configurecoredump"
	"node-agent/ynp/module/provision/configureos"
	"node-agent/ynp/module/provision/configuresudoers"
	"node-agent/ynp/module/provision/configurethp"
	"node-agent/ynp/module/provision/disablednfautomatic"
	"node-agent/ynp/module/provision/disablefirewalld"
	"node-agent/ynp/module/provision/installconfigureearlyoom"
	"node-agent/ynp/module/provision/installpackages"
	"node-agent/ynp/module/provision/mountephemeraldrives"
	"node-agent/ynp/module/provision/network"
	"node-agent/ynp/module/provision/nodeagent"
	"node-agent/ynp/module/provision/nodeexporter"
	"node-agent/ynp/module/provision/ospackageupdate"
	"node-agent/ynp/module/provision/otelcol"
	"node-agent/ynp/module/provision/pglogotelcol"
	"node-agent/ynp/module/provision/rebootnode"
	"node-agent/ynp/module/provision/sshd"
	"node-agent/ynp/module/provision/systemd"
	"node-agent/ynp/module/provision/tailscale"
	"node-agent/ynp/module/provision/teleport"
	"node-agent/ynp/module/provision/ulimitsalma8"
	"node-agent/ynp/module/provision/updateos"
	"node-agent/ynp/module/provision/wazuh"
	"node-agent/ynp/module/provision/ybmami"
	"node-agent/ynp/module/provision/yugabyte"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
)

// OSFamily represents the operating system family.
type OSFamily string

// PackageManager represents the package manager type.
type PackageManager string

const (
	RedHat  OSFamily = "RedHat"
	Debian  OSFamily = "Debian"
	Suse    OSFamily = "Suse"
	Arch    OSFamily = "Arch"
	Unknown OSFamily = "Unknown"

	RPM  PackageManager = "rpm"
	DPKG PackageManager = "dpkg"

	YNPVersionFile = "ynp_version"
)

type ProvisionCommand struct {
	ctx            context.Context
	iniConfig      *config.INIConfig
	args           config.Args
	modules        map[string]config.Module
	osVersion      string
	osFamily       OSFamily
	osDistribution string
	packageManager PackageManager
}

func NewProvisionCommand(
	ctx context.Context,
	iniConfig *config.INIConfig,
	args config.Args,
) config.Command {
	command := &ProvisionCommand{
		ctx:       ctx,
		iniConfig: iniConfig,
		args:      args,
		modules:   make(map[string]config.Module),
	}
	return command
}

// Init initializes the ProvisionCommand.
func (pc *ProvisionCommand) Init() error {
	err := pc.discoverOSInfo()
	if err != nil {
		util.FileLogger().Errorf(pc.ctx, "Failed to discover OS info: %v", err)
		return err
	}
	err = pc.discoverPackageManager()
	if err != nil {
		util.FileLogger().Errorf(pc.ctx, "Failed to discover package manager: %v", err)
		return err
	}
	err = pc.RegisterModules()
	if err != nil {
		util.FileLogger().Errorf(pc.ctx, "Failed to load module: %v", err)
		return err
	}
	return nil
}

// Register the modules after initializing their base paths.
func (pc *ProvisionCommand) RegisterModules() error {
	modulesPath := filepath.Join(pc.args.YnpBasePath, "modules", "provision")
	// Start of YBM specific modules.
	pc.registerModule(configurecoredump.NewConfigureCoredump(modulesPath))
	pc.registerModule(disablednfautomatic.NewDisabledDnfAutomatic(modulesPath))
	pc.registerModule(disablefirewalld.NewDisableFirewalld(modulesPath))
	pc.registerModule(ospackageupdate.NewConfigureOSPackageUpdate(modulesPath))
	pc.registerModule(otelcol.NewConfigureOtelcol(modulesPath))
	pc.registerModule(pglogotelcol.NewConfigurePgLogOtelcol(modulesPath))
	pc.registerModule(tailscale.NewConfigureTailscale(modulesPath))
	pc.registerModule(teleport.NewConfigureTeleport(modulesPath))
	pc.registerModule(ulimitsalma8.NewConfigureUlimitsAlma8(modulesPath))
	pc.registerModule(wazuh.NewConfigureWazuh(modulesPath))
	// End of YBM specific modules.

	pc.registerModule(backuputils.NewBackupUtils(modulesPath))
	pc.registerModule(chrony.NewConfigureChrony(modulesPath))
	pc.registerModule(clockbound.NewConfigureClockbound(modulesPath))
	pc.registerModule(configureos.NewConfigureOs(modulesPath))
	pc.registerModule(configuresudoers.NewConfigureSudoers(modulesPath))
	pc.registerModule(configurethp.NewConfigureTHP(modulesPath))
	pc.registerModule(installconfigureearlyoom.NewInstallConfigureEarlyoom(
		modulesPath,
	))
	pc.registerModule(installpackages.NewInstallPackages(modulesPath))
	pc.registerModule(mountephemeraldrives.NewMountEphemeralDrive(
		modulesPath,
	))
	pc.registerModule(network.NewConfigureNetwork(modulesPath))
	pc.registerModule(nodeagent.NewInstallNodeAgent(modulesPath))
	pc.registerModule(nodeexporter.NewConfigureNodeExporter(modulesPath))
	pc.registerModule(rebootnode.NewRebootNode(modulesPath))
	pc.registerModule(sshd.NewConfigureSshD(modulesPath))
	pc.registerModule(systemd.NewConfigureSystemd(modulesPath))
	pc.registerModule(updateos.NewUpdateOS(modulesPath))
	pc.registerModule(ybmami.NewConfigureYBMAMI(modulesPath))
	pc.registerModule(yugabyte.NewCreateYugabyteUser(modulesPath))

	return nil
}

// Register a single module.
func (pc *ProvisionCommand) registerModule(module config.Module) error {
	pc.modules[module.Name()] = module
	return nil
}

// Returns the list of required OS packages based on the OS family.
func (pc *ProvisionCommand) requiredOSPkgs() []string {
	switch pc.osFamily {
	case RedHat:
		return []string{"openssl", "policycoreutils"}
	case Debian:
		return []string{"openssl", "policycoreutils"}
	case Suse:
		return []string{"openssl"}
	case Arch:
		return []string{"openssl", "policycoreutils"}
	default:
		return []string{}
	}
}

// Returns the list of required packages for cloud environments.
func (pc *ProvisionCommand) requiredCloudOnlyOSPkgs() []string {
	return []string{"gzip"}
}

func (pc *ProvisionCommand) Validate() error {
	return pc.validateRequiredPackages()
}

func (pc *ProvisionCommand) DryRun() error {
	installScript, precheckScript, err := pc.generateTemplate()
	if err != nil {
		return err
	}
	util.ConsoleLogger().Infof(pc.ctx, "Install Script: %s", installScript)
	util.ConsoleLogger().Infof(pc.ctx, "Precheck Script: %s", precheckScript)
	return nil
}

func (pc *ProvisionCommand) RunPreflightChecks() error {
	_, precheckScript, err := pc.generateTemplate()
	if err != nil {
		return err
	}
	if err := pc.compareYnpVersion(); err != nil {
		return err
	}
	return pc.runScript("precheck", precheckScript)
}

func (pc *ProvisionCommand) ListModules() error {
	moduleNames := make([]string, 0, len(pc.modules))
	for moduleName := range pc.modules {
		moduleNames = append(moduleNames, moduleName)
	}
	util.ConsoleLogger().Infof(pc.ctx, "Registered modules: %s", strings.Join(moduleNames, ", "))
	return nil
}

func (pc *ProvisionCommand) Execute() error {
	if err := pc.validateSpecificModules(); err != nil {
		return err
	}
	runScript, precheckScript, err := pc.generateTemplate()
	if err != nil {
		return err
	}
	errMsg := []string{}
	err = pc.runScript("provision", runScript)
	if err != nil {
		errMsg = append(errMsg, fmt.Sprintf("Provisioning failed: %v", err))
	}
	err = pc.runScript("precheck", precheckScript)
	if err != nil {
		errMsg = append(errMsg, fmt.Sprintf("Precheck failed: %v", err))
	}
	if err := pc.saveYnpVersion(); err != nil {
		return err
	}
	if len(errMsg) > 0 {
		return fmt.Errorf("%s", strings.Join(errMsg, "; "))
	}
	return nil
}

func (pc *ProvisionCommand) Cleanup() {}

func (pc *ProvisionCommand) validateSpecificModules() error {
	unknownModules := []string{}
	for _, moduleName := range pc.args.SpecificModules {
		if _, ok := pc.modules[moduleName]; !ok {
			unknownModules = append(unknownModules, moduleName)
		}
	}
	if len(unknownModules) > 0 {
		return fmt.Errorf("unknown modules specified: %s", strings.Join(unknownModules, ", "))
	}
	return nil
}

func (pc *ProvisionCommand) validateRequiredPackages() error {
	pkgs := pc.requiredOSPkgs()
	for _, pkg := range pkgs {
		if err := pc.checkPackage(pkg); err != nil {
			return err
		}
	}
	if config.GetBool(pc.iniConfig.DefaultSectionValue(), "is_cloud", false) {
		for _, pkg := range pc.requiredCloudOnlyOSPkgs() {
			if err := pc.checkPackage(pkg); err != nil {
				return err
			}
		}
	}
	return nil
}

func (pc *ProvisionCommand) discoverPackageManager() error {
	knownPackageManagers := []PackageManager{RPM, DPKG}
	for _, pm := range knownPackageManagers {
		if _, err := exec.LookPath(string(pm)); err == nil {
			pc.packageManager = pm
			return nil
		}
	}
	return fmt.Errorf(
		"Unsupported package manager. Known package managers: %v",
		knownPackageManagers,
	)
}

func (pc *ProvisionCommand) checkPackage(pkg string) error {
	var cmd *exec.Cmd
	switch pc.packageManager {
	case RPM:
		cmd = exec.Command("rpm", "-q", pkg)
	case DPKG:
		cmd = exec.Command("dpkg", "-s", pkg)
	default:
		return nil
	}
	err := cmd.Run()
	if err != nil {
		util.FileLogger().Infof(pc.ctx, "%s is not installed.", pkg)
		return err
	}
	util.FileLogger().Infof(pc.ctx, "%s is installed.", pkg)
	return nil
}

func (pc *ProvisionCommand) runScript(name, scriptPath string) error {
	cmd := exec.Command("/bin/bash", "-lc", scriptPath)
	out, err := cmd.CombinedOutput()
	util.FileLogger().Infof(pc.ctx, "%s(%s) Output: %s", name, scriptPath, string(out))
	if err != nil {
		util.FileLogger().Errorf(pc.ctx, "%s(%s) Error: %v", name, scriptPath, err)
	}
	exitCode := cmd.ProcessState.ExitCode()
	if exitCode != 0 {
		return fmt.Errorf("Script %s(%s) failed with exit code %d", name, scriptPath, exitCode)
	}
	return nil
}

// prepareGenerateTemplate performs any preparation needed before generating templates.
func (pc *ProvisionCommand) prepareGenerateTemplate() error {
	if config.GetBool(pc.iniConfig.DefaultSectionValue(), "is_ybm", false) {
		util.FileLogger().Infof(pc.ctx, "Copying template files for YBM")
		if err := pc.copyTemplatesFilesForYBM(pc.iniConfig.DefaultSectionValue()); err != nil {
			return err
		}
	}
	return nil
}

// generateTemplate generates the install and precheck scripts. If the optional specificModules are
// provided, only those modules are processed.
func (pc *ProvisionCommand) generateTemplate() (string, string, error) {
	allTemplates := make([]*config.RenderedTemplates, 0)
	if err := pc.prepareGenerateTemplate(); err != nil {
		return "", "", err
	}
	// Process in the order of sections in the ini file.
	for _, key := range pc.iniConfig.Sections() {
		if key == config.DefaultINISection {
			continue
		}
		module, ok := pc.modules[key]
		if !ok {
			util.FileLogger().Infof(pc.ctx, "Module not found: %s", key)
			continue
		}
		if len(pc.args.SpecificModules) > 0 && !slices.Contains(pc.args.SpecificModules, key) {
			continue
		}
		if len(pc.args.SkipModules) > 0 && slices.Contains(pc.args.SkipModules, key) {
			continue
		}
		values := pc.iniConfig.SectionValue(key)
		if key == nodeagent.ModuleName && !config.GetBool(values, "is_install_node_agent", false) {
			util.FileLogger().Infof(pc.ctx, "Skipping %s because is_install_node_agent is %v\n",
				key,
				values["is_install_node_agent"],
			)
			continue
		}
		if key == clockbound.ModuleName && !config.GetBool(values, "configure_clockbound", false) {
			util.FileLogger().Infof(pc.ctx, "Skipping %s because %s.configure_clockbound is %v\n",
				key, key, values["configure_clockbound"])
			continue
		}
		if key == configuresudoers.ModuleName &&
			!config.GetBool(values, "sudoers_commands", false) {
			util.FileLogger().Infof(pc.ctx, "Skipping %s because %s.sudoers_commands is not set\n",
				key, key)
			continue
		}
		values["templatedir"] = filepath.Join(filepath.Dir(module.BasePath()), "templates")
		values["os_family"] = pc.osFamily
		values["os_version"] = pc.osVersion
		values["os_distribution"] = pc.osDistribution
		util.FileLogger().Infof(pc.ctx, "Rendering templates for module %s", key)
		rendered, err := module.RenderTemplates(pc.ctx, values)
		if err != nil {
			util.FileLogger().Infof(pc.ctx, "Error rendering templates for module %s: %v", key, err)
			return "", "", err
		}
		if rendered != nil {
			allTemplates = append(allTemplates, rendered)
		}
	}
	runScript, err := pc.buildScript(allTemplates, "run", true /*createSubshell*/)
	if err != nil {
		return "", "", err
	}
	precheckScript, err := pc.buildScript(allTemplates, "precheck", false /*createSubshell*/)
	if err != nil {
		return "", "", err
	}
	return runScript, precheckScript, nil
}

func (pc *ProvisionCommand) addExitCodeCheck(f *os.File, moduleName string) {
	fmt.Fprintf(f, `
		exit_code=$?
        if [ $exit_code -ne 0 ]; then
            parent_exit_code=$exit_code
            err="Module %s failed with code $exit_code"
            errors+=("$err")
            echo "$err"
        fi
       `, moduleName)
}

func (pc *ProvisionCommand) printExitErrors(f *os.File) {
	fmt.Fprintf(f, `
        if [ ${#errors[@]} -ne 0 ]; then
            for err in "${errors[@]}"; do
                echo "$err"
            done
        fi
        exit $parent_exit_code
        `)
}

func (pc *ProvisionCommand) addResultHelper(f *os.File) {
	fmt.Fprintf(f, `
            # Initialize the JSON results array
            json_results='{
"results":[
'
            add_result() {
                local check="$1"
                local result="$2"
                local message="$3"
                if [ "${#json_results}" -gt 20 ]; then
                    json_results+=',
'
                fi
                json_results+='    {
'
                json_results+='      "check": "'$check'",
'
                json_results+='      "result": "'$result'",
'
                json_results+='      "message": "'$message'"
'
                json_results+='    }'
            }
	`)
}

func (pc *ProvisionCommand) printResultHelper(f *os.File) {
	fmt.Fprintf(f, `
            print_results() {
                any_fail=0
                if [[ $json_results == *'"result": "FAIL"'* ]]; then
                    any_fail=1
                fi
                json_results+='
]}'

                # Output the JSON
                echo "$json_results"

                # Exit with status code 1 if any check has failed
                if [ $any_fail -eq 1 ]; then
                    echo "Pre-flight checks failed, Please fix them before continuing."
                    exit 1
                else
                    echo "Pre-flight checks successful"
                fi
            }

            print_results
			`)
}

func (pc *ProvisionCommand) populateSudoCheck(f *os.File) {
	fmt.Fprintf(f, "\n######## Check the SUDO Access #########\n")
	fmt.Fprintf(f, "SUDO_ACCESS=\"false\"\n")
	fmt.Fprintf(f, "if [ $(id -u) = 0 ]; then\n")
	fmt.Fprintf(f, "  SUDO_ACCESS=\"true\"\n")
	fmt.Fprintf(f, "elif sudo -n pwd >/dev/null 2>&1; then\n")
	fmt.Fprintf(f, "  SUDO_ACCESS=\"true\"\n")
	fmt.Fprintf(f, "fi\n")
}

func (pc *ProvisionCommand) buildScript(
	allTemplates []*config.RenderedTemplates,
	phase string,
	createSubshell bool,
) (string, error) {
	defaultValue := pc.iniConfig.DefaultSectionValue()
	dir := "/tmp"
	if tmp, ok := defaultValue["tmp_directory"].(string); ok {
		dir = tmp
	}
	f, err := os.CreateTemp(dir, "*.sh")
	if err != nil {
		return "", err
	}
	defer f.Close()
	f.WriteString("#!/bin/bash\n\n")
	if defaultValue["loglevel"] == "DEBUG" {
		f.WriteString("set -x\n")
	}
	if createSubshell {
		// Initialize parent exit code and errors array.
		fmt.Fprintf(f, "parent_exit_code=0\n")
		fmt.Fprintf(f, "errors=()\n")
	} else {
		// Result helper function works only in the same shell.
		pc.addResultHelper(f)
	}
	pc.populateSudoCheck(f)
	for _, tmpl := range allTemplates {
		fmt.Fprintf(f, "\n######## BEGIN %s #########\n", tmpl.Name())
		if rendered := tmpl.RenderedContent(phase); strings.TrimSpace(rendered) != "" {
			if createSubshell {
				fmt.Fprint(f, "(\n")
			}
			fmt.Fprintf(f, "echo \"Executing module %s\"\n", tmpl.Name())
			fmt.Fprint(f, rendered)
			if createSubshell {
				fmt.Fprint(f, "\n)\n")
				pc.addExitCodeCheck(f, tmpl.Name())
			}
		}
		fmt.Fprintf(f, "\n######## END %s #########\n", tmpl.Name())
	}
	if createSubshell {
		fmt.Fprintf(f, "\n######## Summary #########\n")
		pc.printExitErrors(f)
	} else {
		pc.printResultHelper(f)
	}
	os.Chmod(f.Name(), 0755)
	util.FileLogger().Infof(pc.ctx, "Temp file for %s is: %s", phase, f.Name())
	return f.Name(), nil
}

func (pc *ProvisionCommand) discoverOSInfo() error {
	osRelease := "/etc/os-release"
	data, err := os.ReadFile(osRelease)
	if err != nil {
		return err
	}
	lines := strings.Split(string(data), "\n")
	info := make(map[string]string)
	for _, line := range lines {
		if strings.Contains(line, "=") {
			parts := strings.SplitN(line, "=", 2)
			info[parts[0]] = strings.Trim(parts[1], `"`)
		}
	}
	pc.osDistribution = strings.ToLower(info["ID"])
	versionId := info["VERSION_ID"]
	if versionId != "" {
		// Take the major version only.
		pc.osVersion = strings.Split(versionId, ".")[0]
	}
	pc.osFamily = Unknown
	switch pc.osDistribution {
	case "rhel", "centos", "almalinux", "ol", "fedora":
		pc.osFamily = RedHat
	case "ubuntu", "debian":
		pc.osFamily = Debian
	case "suse", "opensuse", "sles":
		pc.osFamily = Suse
	case "arch":
		pc.osFamily = Arch
	default:
		pc.osFamily = Unknown
	}
	return nil
}

// Visible for testing only.
func (pc *ProvisionCommand) SetOSInfo(
	family OSFamily,
	distro, version string,
	packageManager PackageManager,
) {
	pc.osFamily = family
	pc.osDistribution = distro
	pc.osVersion = version
	pc.packageManager = packageManager
}

func (pc *ProvisionCommand) copyTemplatesFilesForYBM(ctx map[string]any) error {
	ynpDir, _ := ctx["ynp_dir"].(string)
	modulesPath := filepath.Join(ynpDir, "modules/provision")
	systemdDir := filepath.Join(modulesPath, "systemd/templates/")
	ybmDir := filepath.Join(modulesPath, "ybm_ami/templates/")
	files := []string{
		"clean_cores.sh.j2",
		"zip_purge_yb_logs.sh.j2",
		"collect_metrics_wrapper.sh.j2",
	}
	for _, f := range files {
		src := filepath.Join(systemdDir, f)
		dest := filepath.Join(ybmDir, f)
		data, err := os.ReadFile(src)
		if err != nil {
			util.FileLogger().Errorf(pc.ctx, "Failed to read file %s: %v", src, err)
			return err
		}
		err = os.WriteFile(dest, data, 0644)
		if err != nil {
			util.FileLogger().Errorf(pc.ctx, "Failed to write file %s: %v", dest, err)
			return err
		}
	}
	return nil
}

func (pc *ProvisionCommand) saveYnpVersion() error {
	currentYnpVersion, _ := pc.iniConfig.DefaultSectionValue()["version"].(string)
	ybHomeDir, _ := pc.iniConfig.DefaultSectionValue()["yb_home_dir"].(string)
	ybUser, _ := pc.iniConfig.DefaultSectionValue()["yb_user"].(string)
	if currentYnpVersion == "" || ybHomeDir == "" {
		util.FileLogger().
			Info(pc.ctx, "yb_home_dir or current version file is missing in the context")
		return nil
	}
	// Ensure yb_home_dir exists.
	if err := os.MkdirAll(ybHomeDir, 0755); err != nil {
		return err
	}

	ynpVersionFile := filepath.Join(ybHomeDir, YNPVersionFile)
	if err := os.WriteFile(ynpVersionFile, []byte(currentYnpVersion), 0644); err != nil {
		util.FileLogger().Errorf(pc.ctx, "Failed to write YNP version to file: %v", err)
		return err
	}
	if details, err := util.UserInfo(ybUser); err == nil {
		if details.CurrentUserID == 0 && !details.IsCurrent {
			// Change ownership only if running as root and yb_user is different from current user.
			if err := os.Chown(ynpVersionFile, int(details.UserID), int(details.GroupID)); err != nil {
				util.FileLogger().
					Errorf(pc.ctx, "Cannot change ownership of version file: %v", err)
				return err
			}
		}
	} else {
		util.FileLogger().Errorf(pc.ctx, "Cannot find user info for %s: %v", ybUser, err)
		return err
	}
	return nil
}

// Parse a version string like "1.2.3" into [major, minor, patch]
func parseVersion(version string) ([3]int, error) {
	var result [3]int
	parts := strings.Split(version, ".")
	if len(parts) != 3 {
		return result, fmt.Errorf("Invalid version format: %s", version)
	}
	for i, part := range parts {
		n, err := strconv.Atoi(part)
		if err != nil {
			return result, fmt.Errorf("Invalid version format: %s", version)
		}
		result[i] = n
	}
	return result, nil
}

func (pc *ProvisionCommand) compareYnpVersion() error {
	ybHomeDir, _ := pc.iniConfig.DefaultSectionValue()["yb_home_dir"].(string)
	versionStr, _ := pc.iniConfig.DefaultSectionValue()["version"].(string)
	if ybHomeDir == "" || versionStr == "" {
		err := fmt.Errorf("yb_home_dir or version missing in context")
		util.FileLogger().Errorf(pc.ctx, "Error: %v", err)
		return err
	}

	currentYnpVersion, err := parseVersion(versionStr)
	if err != nil {
		util.FileLogger().Errorf(pc.ctx, "Unable to parse current YNP version: %v", err)
		return err
	}

	ynpVersionFile := ybHomeDir + "/ynp_version"
	data, err := os.ReadFile(ynpVersionFile)
	if err != nil {
		util.FileLogger().Errorf(pc.ctx, "The ynp_version file was not found at %s", ynpVersionFile)
		return err
	}
	storedYnpVersion, err := parseVersion(strings.TrimSpace(string(data)))
	if err != nil {
		util.FileLogger().Errorf(pc.ctx, "Error parsing version from the ynp_version file: %v", err)
		return err
	}

	if currentYnpVersion[0] != storedYnpVersion[0] {
		err := fmt.Errorf(
			"The major versions are different. Current: %v, Stored: %v. Please run reprovision again on the node",
			currentYnpVersion,
			storedYnpVersion,
		)
		util.FileLogger().Errorf(pc.ctx, "Error: %v", err)
		return err
	}
	return nil
}
