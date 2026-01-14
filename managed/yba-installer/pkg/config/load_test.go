package config

import (
	"bytes"
	"testing"

	"github.com/spf13/viper"
)

func TestLegacyToRootConfig(t *testing.T) {
	configStr := `
installRoot: /opt/yugabyte
service_username: yugabyte
host: "shubin-host,host2"
server_key_path: ""
server_cert_path: ""
as_root: true
wait_for_yba_ready_seconds: 300

platform:
  port: 7000
  hsts_enabled: true
  custom_headers: []
  disabled_tls_algorithms: []
  useOauth: false
  ybSecurityType: ""
  ybOidcClientId: ""
  ybOidcSecret: ""
  ybOidcDiscoveryUri: ""
  ywUrl: ""
  ybOidcScope: ""
  ybOidcEmailAttr: ""	
  keyStorePassword: ""
  appSecret: ""
  restartSeconds: 60
  proxy:
    enabled: false
    https_port: 443
    http_port: 80
    http_proxy: ""
    https_proxy: ""
    no_proxy: ""
    java_non_proxy: ""
  support_origin_url: ""
  additional_config: |

postgres:
  install:
    enabled: true
    username: posgres
    password: ""
    port: 5434
    restartSeconds: 10
    locale: en_US.UTF-8
    ldap_enabled: false
    ldap_server: ""
    ldap_prefix: ""
    ldap_suffix: ""
    ldap_port: 389
    secure_ldap: false
  existing:
    enabled: false
    username: ""
    password: ""
    host: ""
    port: 5432
    pg_dump_path: ""
    pg_restore_path: ""
prometheus:
   port: 9090
   restartSeconds: 10        # time (in seconds) to sleep before restarting prometheus service
   scrapeInterval: 10s       # time between prometheus data scrapes
   scrapeTimeout: 10s        # time before a scrape request times out
   maxConcurrency: 20        # max number of queries to execute concurrently
   maxSamples: 5000000       # max number of samples prometheus can load to process single query
   timeout: 30s              # time before a query times out
   retentionTime: 15d        # time to retain metrics
   enableHttps: false        # enable HTTPS for prometheus web UI
   enableAuth: false         # enable authentication for prometheus
   authUsername: prometheus  # username to authenticate to prometheus (if enableAuth)
   authPassword: ""          # password to authenticate to prometheus (if enableAuth)
   oomScoreAdjust: 500       # OOMScoreAdjust value for the prometheus systemd service
   allowedTLSCiphers: [
      "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256",
      "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384",
      "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
      "TLS_AES_128_GCM_SHA256",
      "TLS_AES_256_GCM_SHA384",
      "TLS_CHACHA20_POLY1305_SHA256",
      "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256",
      "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384",
      "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
      "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256"
   ] # Supported Ciphers for TLS handshake

   # Remote Write config as yaml.
   remoteWrite:
      enabled: false
      configs: []

   # Custom config for different scrape config jobs
   # Scheme must be http or https
   scrapeConfig:
      # Scraping from node agent
      node-agent:
         scheme: http
      # Scraping from node exporter on db nodes
      node:
         scheme: http
      # Scraping yugabyteDB metrics
      yugabyte:
         scheme: http

perfAdvisor:
   enabled: false
   port: 8443
   restartSeconds: 10
   enableHttps: false
`
	v := viper.New()
	v.SetConfigType("yaml")
	err := v.ReadConfig(bytes.NewBufferString(configStr))
	if err != nil {
		t.Fatalf("Failed to read config: %v", err)
	}
	cfg := legacyToRootConfig(v)
	// Validate top level legacy config moves to installer sub-config
	if cfg.Installer.InstallRoot != "/opt/yugabyte" {
		t.Errorf("Expected InstallRoot to be '/opt/yugabyte', got '%s'", cfg.Installer.InstallRoot)
	}
	// Validate platform config
	if cfg.Platform.Port != 7000 {
		t.Errorf("Expected Platform Port to be 7000, got %d", cfg.Platform.Port)
	}
	// Validate postgres config gets migrated
	if cfg.Postgres.Port != 5434 {
		t.Errorf("Expected Postgres Install Port to be 5434, got %d", cfg.Postgres.Port)
	}
	if cfg.Postgres.Username != "posgres" {
		t.Errorf("Expected Postgres Install Username to be 'posgres', got '%s'", cfg.Postgres.Username)
	}
	// TODO: add more check to validate the difference
}
