package replflow

import (
	"crypto/tls"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replicatedctl"
)

// stopContainer runs docker stop on the container
func stopContainer(name string) error {
	if out := shell.Run("docker", "stop", name); !out.Succeeded() {
		if strings.Contains(out.StderrString(), "No such container") {
			logging.Debug("skipping stop of container " + name + " - doesn't exist")
			return nil
		}
		out.SucceededOrLog()
		return fmt.Errorf("could not stop container %s: %w", name, out.Error)
	}
	return nil
}

// forceDeleteContainers removes containers with 'docker rm -f'
func forceDeleteContainer(containers ...string) error {
	args := append([]string{"rm", "-f"}, containers...)
	if out := shell.Run("docker", args...); !out.SucceededOrLog() {
		return fmt.Errorf("could not force delete containers %s: %w",
			strings.Join(containers, ", "), out.Error)
	}
	return nil
}

// deleteContainerImagesRegex deletes all images matching the given regex
func deleteContainerImagesRegex(regex string) error {
	cmdArgs := fmt.Sprintf(`images | grep "%s" | awk '{print $3}' | xargs docker rmi -f`, regex)
	if out := shell.RunShell("docker", cmdArgs); !out.Succeeded() {
		if strings.Contains(out.StderrString(), "requires at least 1 argument") {
			logging.Debug("found no images matching regex " + regex)
			return nil
		}
		out.SucceededOrLog()
		return fmt.Errorf("could not delete images matching regex %s: %w", regex, out.Error)
	}
	return nil
}

// YbaVersion queries the yba version running in replicated using the api.
func YbaVersion(config replicatedctl.AppConfig) (string, error) {
	// Check if need http or https, while also getting the port
	var urlSchema string
	var port int
	var portEntry replicatedctl.ConfigEntry
	isHttpsEntry := config.Get("https_enabled")
	if isHttpsEntry == replicatedctl.NilConfigEntry {
		return "", fmt.Errorf("no https_enabled found")
	}
	https, err := isHttpsEntry.Bool()
	if err != nil {
		return "", err
	}
	if https {
		urlSchema = "https"
		portEntry = config.Get("ui_https_port")
	} else {
		urlSchema = "http"
		portEntry = config.Get("ui_http_port")
	}
	if portEntry == replicatedctl.NilConfigEntry {
		return "", fmt.Errorf("no %s port entry found", urlSchema)
	}
	port, err = portEntry.Int()
	if err != nil {
		return "", fmt.Errorf("failed to parse port: %w", err)
	}

	url := fmt.Sprintf("%s://localhost:%d/api/v1/app_version", urlSchema, port)
	http.DefaultTransport.(*http.Transport).TLSClientConfig = &tls.Config{InsecureSkipVerify: true}
	resp, err := http.Get(url)
	if err != nil {
		return "", fmt.Errorf("failed to get yba version: %w", err)
	}
	defer resp.Body.Close()
	var result map[string]string
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return "", fmt.Errorf("unable to parse version: %w", err)
	}
	return result["version"], nil
}
