/*
* Copyright (c) YugabyteDB, Inc.
 */

package template

import (
	"fmt"
	"testing"

	"github.com/spf13/viper"
	"sigs.k8s.io/yaml"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

// renderPrometheusWebConf renders the prometheus template from the source tree and returns the
// parsed web.yml it would write.
func renderPrometheusWebConf(t *testing.T, enableHttps, enableHsts bool) map[string]any {
	t.Helper()
	oldVersion := common.Version
	t.Cleanup(func() { common.Version = oldVersion; viper.Reset() })
	common.Version = "2.0.0.0-b1" // normally set by ldflags; logrotateD needs it
	viper.Set("host", "localhost")
	viper.Set("prometheus.enableHttps", enableHttps)
	if enableHsts {
		viper.Set("prometheus.enableHsts", true)
	}

	dirs := struct {
		SystemdFileLocation, ConfFileLocation, WebConfFile, DataDir, PromDir, LogDir string
		HttpsCertPath, HttpsKeyPath                                                  string
	}{
		WebConfFile:   "/tmp/web.yml",
		HttpsCertPath: "/tmp/server.crt",
		HttpsKeyPath:  "/tmp/server.key",
	}
	rendered, err := renderTemplate("../../config/templates/yba-installer-prometheus.yml", dirs)
	if err != nil {
		t.Fatal(err)
	}
	jsonData, err := readYAMLtoJSON(rendered)
	if err != nil {
		t.Fatal(err)
	}
	for _, s := range jsonData["services"].([]any) {
		service := s.(map[string]any)
		if service["name"] != "prometheusWebConf" {
			continue
		}
		webConf := map[string]any{}
		if err := yaml.Unmarshal([]byte(fmt.Sprint(service["contents"])), &webConf); err != nil {
			t.Fatalf("web.yml is not valid yaml: %v\n%s", err, service["contents"])
		}
		return webConf
	}
	t.Fatal("prometheusWebConf service not found in template")
	return nil
}

func TestPrometheusWebConfHsts(t *testing.T) {
	webConf := renderPrometheusWebConf(t, true, true)
	if _, ok := webConf["tls_server_config"]; !ok {
		t.Fatalf("tls_server_config missing with HTTPS on: %v", webConf)
	}
	server, _ := webConf["http_server_config"].(map[string]any)
	headers, _ := server["headers"].(map[string]any)
	if got := headers["Strict-Transport-Security"]; got != "max-age=31536000; includeSubDomains" {
		t.Fatalf("Strict-Transport-Security = %v, web.yml: %v", got, webConf)
	}
}

// Upgraded installs have no enableHsts key, so a missing key must mean off.
func TestPrometheusWebConfNoHstsByDefault(t *testing.T) {
	webConf := renderPrometheusWebConf(t, true, false)
	if _, ok := webConf["tls_server_config"]; !ok {
		t.Fatalf("tls_server_config missing with HTTPS on: %v", webConf)
	}
	if _, ok := webConf["http_server_config"]; ok {
		t.Fatalf("http_server_config set with enableHsts off: %v", webConf)
	}
}

// Browsers ignore HSTS over plain HTTP, so enableHsts alone must not add anything.
func TestPrometheusWebConfNoHstsWithoutHttps(t *testing.T) {
	if webConf := renderPrometheusWebConf(t, false, true); len(webConf) != 0 {
		t.Fatalf("web.yml not empty with HTTPS off: %v", webConf)
	}
}
