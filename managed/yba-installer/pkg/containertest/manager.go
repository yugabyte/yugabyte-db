package containertest

import (
	"fmt"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
)

type Manager interface {
	Start(name string, cfg Config) ContainerRef
	Stop(ContainerRef) error
	Exec(ctr ContainerRef, cmds ...string) *shell.Output
	IsContainerRunning(ContainerRef) (bool, error)
	CopyTo(ctr ContainerRef, localSrc, ctrDest string) error
	CopyFrom(ctr ContainerRef, ctrSrc, localDest string) error
	BuildImage(dockerfilePath, imageName, tag string) error
}

func NewManager() (Manager, error) {
	containerType := getContainerType()
	if err := containerType.Validate(); err != nil {
		return nil, err
	}
	switch containerType {
	case DockerContainerType:
		return &DockerMgr{}, nil
	case PodmanContainerType:
		return nil, fmt.Errorf("podman support is not implemented yet")
	default:
		// NOTE: We should not fall into default, as we validate the container type above.
		return nil, fmt.Errorf("unknown container type: %s", containerType)
	}
}
