package containertest

type Config struct {
	// Pass through systemd filesystem and allow systemd in the container
	Systemd bool

	// The image to use for the container. Should be a valid Docker image name
	// EX: almalinux:8 or ubuntu:latest
	BaseImage string

	// Extra volumes to mount into the container.
	// NOTE: systemd related volumes are automatically added with Systemd = true
	Volumes []Volume

	// Ports to expose from the container.
	Ports []Port
}

func (c Config) AddVolume(hostPath, containerPath string, readOnly bool) Config {
	if c.Volumes == nil {
		c.Volumes = make([]Volume, 5)
	}
	c.Volumes = append(c.Volumes, Volume{
		HostPath:      hostPath,
		ContainerPath: containerPath,
		ReadOnly:      readOnly,
	})
	return c
}

func (c Config) AddPort(hostPort, containerPort int) Config {
	if c.Ports == nil {
		c.Ports = make([]Port, 0, 5)
	}
	c.Ports = append(c.Ports, Port{
		HostPort:      hostPort,
		ContainerPort: containerPort,
	})
	return c
}

type Volume struct {
	// Host path to mount into the container
	HostPath string
	// Container path to mount the host path into
	ContainerPath string
	// ReadOnly if true, the volume will be mounted as read-only
	ReadOnly bool
}

type Port struct {
	// Host port to expose
	HostPort int
	// Container port to expose
	ContainerPort int
}

var DefaultConfig = Config{
	Systemd:   true,
	BaseImage: "almalinux:8",
}

func NewConfig() Config {
	return Config{
		Systemd:   DefaultConfig.Systemd,
		BaseImage: DefaultConfig.BaseImage,
		Volumes:   make([]Volume, 0, 5),
	}
}
