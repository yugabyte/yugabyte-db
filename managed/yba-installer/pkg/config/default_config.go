package config

import "github.com/spf13/viper"

func addDefaults(v *viper.Viper) {
	// Installer
	v.SetDefault("installer.install_root", "/opt/yugabyte")
	v.SetDefault("installer.bind_address", "")
	v.SetDefault("installer.wait_for_yba_ready_seconds", 300)
	v.SetDefault("installer.service_username", "yugabyte")
	// Certs (squashed with installer)
	v.SetDefault("installer.server_cert_path", "")
	v.SetDefault("installer.server_key_path", "")

	// Platform
	v.SetDefault("platform.port", 443)
	v.SetDefault("platform.hsts_enabled", true)
	v.SetDefault("platform.custom_headers", []string{})
	v.SetDefault("platform.tls_disabled_algorithms", []string{"DHE keySize < 2048"})
	v.SetDefault("platform.oauth.enabled", false)
	v.SetDefault("platform.oauth.security_type", "")
	v.SetDefault("platform.oauth.oidc_client_id", "")
	v.SetDefault("platform.oauth.oidc_client_secret", "")
	v.SetDefault("platform.oauth.oidc_discovery_uri", "")
	v.SetDefault("platform.oauth.oidc_scope", "")
	v.SetDefault("platform.oauth.yw_url", "")
	v.SetDefault("platform.oauth.oidc_email_attr", "")
	v.SetDefault("platform.keystore_password", "")
	v.SetDefault("platform.app_secret", "")
	v.SetDefault("platform.restart_seconds", 10)
	v.SetDefault("platform.proxy.enabled", false)
	v.SetDefault("platform.proxy.http_proxy_url", "")
	v.SetDefault("platform.proxy.https_proxy_url", "")
	v.SetDefault("platform.proxy.no_proxy", "")
	v.SetDefault("platform.proxy.java_non_proxy_hosts", "")
	v.SetDefault("platform.support_origin_url", "/")
	v.SetDefault("platform.additional_config", "")
	v.SetDefault("platform.trusted_proxies", []string{})

	// Postgres
	v.SetDefault("postgres.port", 5432)
	v.SetDefault("postgres.username", "postgres")
	v.SetDefault("postgres.password", "")
	v.SetDefault("postgres.restart_seconds", 10)
	v.SetDefault("postgres.ldap.enabled", false)
	v.SetDefault("postgres.ldap.server", "")
	v.SetDefault("postgres.ldap.port", 389)
	v.SetDefault("postgres.ldap.prefix", "")
	v.SetDefault("postgres.ldap.suffix", "")
	v.SetDefault("postgres.ldap.secure", false)
	v.SetDefault("postgres.host", "")
	v.SetDefault("postgres.pg_dump_path", "")
	v.SetDefault("postgres.pg_restore_path", "")

	// Prometheus
	v.SetDefault("prometheus.port", 9090)
	v.SetDefault("prometheus.restart_seconds", 10)
	v.SetDefault("prometheus.scrape_interval", "10s")
	v.SetDefault("prometheus.scrape_timeout", "10s")
	v.SetDefault("prometheus.max_concurrency", 20)
	v.SetDefault("prometheus.max_samples", 5000000)
	v.SetDefault("prometheus.timeout", "30s")
	v.SetDefault("prometheus.retention_time", "15d")
	v.SetDefault("prometheus.enable_https", false)
	v.SetDefault("prometheus.enable_auth", false)
	v.SetDefault("prometheus.auth_username", "prometheus")
	v.SetDefault("prometheus.auth_password", "")
	v.SetDefault("prometheus.oom_score_adjust", 500)
	v.SetDefault("prometheus.allowed_tls_ciphers", []string{
		"TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256",
		"TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384",
		"TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
		"TLS_AES_128_GCM_SHA256",
		"TLS_AES_256_GCM_SHA384",
		"TLS_CHACHA20_POLY1305_SHA256",
		"TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256",
		"TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384",
		"TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
		"TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256",
	})
	v.SetDefault("prometheus.remote_write.enabled", false)
	v.SetDefault("prometheus.remote_write.configs", "")
	v.SetDefault("prometheus.scrape_config.node-agent.scheme", "http")
	v.SetDefault("prometheus.scrape_config.node.scheme", "http")
	v.SetDefault("prometheus.scrape_config.yugabyte.scheme", "http")

	// PerfAdvisor (no documented fields)
	// v.SetDefault("performance_advisor.<field>", <value>) // Not set, no info
	v.SetDefault("perfAdvisor.enabled", false)
	v.SetDefault("perfAdvisor.port", 8443)
	v.SetDefault("perfAdvisor.restart_seconds", 10)
	v.SetDefault("perfAdvisor.callhome.enabled", true)
	v.SetDefault("perfAdvisor.callhome.environment", "dev")
	v.SetDefault("perfAdvisor.pa_secret", "")
	v.SetDefault("perfAdvisor.tls.enabled", true)
	v.SetDefault("perfAdvisor.tls.ssl_protocols", "")
	v.SetDefault("perfAdvisor.tls.hsts", true)
	v.SetDefault("perfAdvisor.tls.keystore_password", "")

	// Services (installerConfig.Services)
	v.SetDefault("installer.services", []string{"postgres", "prometheus", "platform"})
}
