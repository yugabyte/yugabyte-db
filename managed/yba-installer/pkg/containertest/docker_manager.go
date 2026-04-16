package containertest

import (
	"context"
	"errors"
	"fmt"
	"os/exec"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
)

var (
	ErrContainerAlreadyRunning = errors.New("container is already running")
)

type DockerMgr struct {
}

func (d *DockerMgr) Start(ctrName string, cfg Config) ContainerRef {
	ctrRef := ContainerRef{
		ContainerName: ctrName,
	}
	go d.start(ctrName, cfg)
	return ctrRef
}

func (d *DockerMgr) Stop(ctr ContainerRef) error {
	cmd := exec.CommandContext(context.TODO(), "docker", "stop", ctr.ContainerName)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to stop container %s: %w", ctr.ContainerName, err)
	}
	return nil
}

func (d *DockerMgr) IsContainerRunning(ctr ContainerRef) (bool, error) {
	cmd := exec.CommandContext(context.TODO(), "docker", "ps", "-q", "--filter", fmt.Sprintf("name=%s", ctr.ContainerName))
	out, err := cmd.Output()
	if err != nil {
		return false, fmt.Errorf("failed to check if container %s is running: %w", ctr.ContainerName, err)
	}
	return len(out) > 0, nil
}

func (d *DockerMgr) Exec(ctr ContainerRef, cmds ...string) *shell.Output {
	cmdParts := []string{"exec", "-i", ctr.ContainerName}
	cmdParts = append(cmdParts, cmds...)
	output := shell.Run("docker", cmdParts...)
	return output
}

func (d *DockerMgr) CopyTo(ctr ContainerRef, localSrc, ctrDest string) error {
	cmd := exec.CommandContext(context.TODO(), "docker", "cp", localSrc, fmt.Sprintf("%s:%s", ctr.ContainerName, ctrDest))
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to copy to container with cmd '%s', %s: %w", cmd.Args, ctr.ContainerName, err)
	}
	return nil
}

func (d *DockerMgr) CopyFrom(ctr ContainerRef, ctrSrc, localDest string) error {
	cmd := exec.CommandContext(context.TODO(), "docker", "cp", fmt.Sprintf("%s:%s", ctr.ContainerName, ctrSrc), localDest)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to copy from container %s: %w", ctr.ContainerName, err)
	}
	return nil
}

func (d *DockerMgr) BuildImage(dockerfilePath, imageName, tag string) error {
	cmd := exec.CommandContext(context.TODO(), "docker", "build", "-f", dockerfilePath, "-t", fmt.Sprintf("%s:%s", imageName, tag), ".")
	output, err := cmd.CombinedOutput()
	if err != nil {
		fmt.Println(string(output))
		return fmt.Errorf("failed to build Docker image %s:%s: %w\nOutput: %s", imageName, tag, err, output)
	}
	fmt.Println("Docker image built successfully:", imageName, tag)
	return nil
}

func (d *DockerMgr) start(ctrName string, cfg Config) {
	var cmdBuilder strings.Builder
	cmdBuilder.WriteString("docker run")

	cmds := []string{"run"}
	cmds = append(cmds, fmt.Sprintf("--name=%s", ctrName))
	cmds = append(cmds, "--rm") // Delete on exit
	if cfg.Systemd {
		cmds = append(cmds, "--tmpfs", "/run")
		cmds = append(cmds, "--tmpfs", "/tmp:exec")                   // Temporary file systems
		cmds = append(cmds, "-v", "/sys/fs/cgroup:/sys/fs/cgroup:ro") // Read-only cgroup mount
		cmds = append(cmds, "-i")                                     // Interactive terminal
	}
	for _, vol := range cfg.Volumes {
		volOption := fmt.Sprintf("%s:%s", vol.HostPath, vol.ContainerPath)
		if vol.ReadOnly {
			volOption += ":ro" // Read-only mount
		}
		cmds = append(cmds, "-v", volOption)
	}
	for _, port := range cfg.Ports {
		portOption := fmt.Sprintf("%d:%d", port.HostPort, port.ContainerPort)
		cmds = append(cmds, "-p", portOption) // Port mapping
	}
	cmds = append(cmds, cfg.BaseImage)    // Image to use
	cmds = append(cmds, "/usr/sbin/init") // Command to run in the container
	// Execute the command
	fmt.Println("Running Docker command:", strings.Join(cmds, " "))
	output := shell.Run("docker", cmds...)
	if output.Error != nil {
		fmt.Println("Docker run exited with error: + ", output.Error.Error())
	}
}
