package containertest

import (
	"fmt"
	"os/exec"
)

type ContainerType string

func (c ContainerType) String() string {
	return string(c)
}

func (c ContainerType) Validate() error {
	switch c {
	case DockerContainerType, PodmanContainerType:
		return nil
	default:
		return fmt.Errorf("invalid container type: %s", c)
	}
}

var (
	DockerContainerType ContainerType = "docker"
	PodmanContainerType ContainerType = "podman"
	NullContainerType   ContainerType = "null"
)

func getContainerType() ContainerType {
	// Check for docker
	if _, err := exec.LookPath("docker"); err == nil {
		return DockerContainerType
	}
	// Check for podman
	if _, err := exec.LookPath("podman"); err == nil {
		return PodmanContainerType
	}
	return NullContainerType
}
