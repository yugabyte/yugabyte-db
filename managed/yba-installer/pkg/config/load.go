package config

import (
	"fmt"
	"path/filepath"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Load the config from either the legacy config file (yba-ctl.yml) or the new config file
// (config.yml|yaml). Parse the config into the new rootConfig structure.

func Load() (rootConfig, error) {
	// We use viper to parse the configuration file(s).
	var cfg rootConfig
	legacy, err := loadLegacyConfig()
	if err != nil {
		return cfg, fmt.Errorf("failed to load legacy config (yba-ctl.yml) file: %w", err)
	}
	if legacy != nil {
		if err := legacy.Unmarshal(&cfg); err != nil {
			return cfg, fmt.Errorf("failed to unmarshal legacy config (yba-ctl.yml): %w", err)
		}
		return cfg, nil
	}

	config, err := loadConfig()
	if err != nil {
		return cfg, fmt.Errorf("failed to load legacy config (config.yml) file: %w", err)
	}
	if config != nil {
		if err := config.Unmarshal(&cfg); err != nil {
			return cfg, fmt.Errorf("failed to unmarshal new config (config.yml): %w", err)
		}
		return cfg, nil
	}
	// TODO: Default config
	return rootConfig{}, nil
}

func loadLegacyConfig() (*viper.Viper, error) {
	if !common.Exists(common.InputFile()) {
		logging.Debug("Legacy config file (yba-ctl.yml) does not exist, skipping legacy config load.")
		return nil, nil
	}
	logging.Info("Loading legacy config file (yba-ctl.yml)")
	legacy := viper.New()
	viper.SetDefault("service_username", common.DefaultServiceUser)
	viper.SetDefault("installRoot", "/opt/yugabyte")
	viper.SetDefault("as_root", common.HasSudoAccess())

	viper.SetDefault("perfAdvisor.enabled", false)
	viper.SetDefault("perfAdvisor.port", 8443)
	viper.SetDefault("perfAdvisor.restartSeconds", 10)
	viper.SetDefault("perfAdvisor.callhome.enabled", true)
	viper.SetDefault("perfAdvisor.callhome.environment", "dev")
	viper.SetDefault("perfAdvisor.paSecret", "")
	viper.SetDefault("perfAdvisor.tls.enabled", true)
	viper.SetDefault("perfAdvisor.tls.sslProtocols", "")
	viper.SetDefault("perfAdvisor.tls.hsts", true)
	viper.SetDefault("perfAdvisor.tls.keystorePassword", "")

	viper.SetDefault("prometheus.remoteWrite.enabled", false)
	viper.SetDefault("prometheus.scrapeConfig.node.scheme", "http")
	viper.SetDefault("prometheus.scrapeConfig.node-agent.scheme", "http")
	viper.SetDefault("prometheus.scrapeConfig.yugabyte.scheme", "http")
	// Update the installRoot to home directory for non-root installs. Will honor custom install root.
	if !common.HasSudoAccess() && viper.GetString("installRoot") == "/opt/yugabyte" {
		viper.SetDefault("installRoot", filepath.Join(common.GetUserHomeDir(), "yugabyte"))
	}
	legacy.SetConfigFile(common.InputFile())
	if err := legacy.ReadInConfig(); err != nil {
		return nil, err
	}
	return legacy, nil
}

// The legacy config has a different structure, so we need to convert it to the new structure.
// In addition, we assume the majority of the legacy config is filled out in the config file,
// so we do not need default values for most fields.
func legacyToRootConfig(legacy *viper.Viper) rootConfig {
	services := []Service{ServicePlatform, ServicePrometheus}
	if legacy.GetBool("postgres.install.enabled") {
		services = append(services, ServicePostgres)
	}
	if legacy.GetBool("perfAdvisor.enabled") {
		services = append(services, ServicePerformanceAdvisor)
	}
	var pgConfig postgresConfig
	if legacy.GetBool("postgres.install.enabled") {
		// If postgres is enabled, we need to set the postgres config.
		pgConfig = postgresConfig{
			postgresBase: postgresBase{
				Username: legacy.GetString("postgres.install.username"),
				Password: legacy.GetString("postgres.install.password"),
				Port:     legacy.GetInt("postgres.install.port"),
			},
			postgresInstall: postgresInstall{
				RestartSeconds: legacy.GetInt("postgres.restartSeconds"),
				LDAP: ldapConfig{
					Enabled: legacy.GetBool("postgres.ldap_enabled"),
					Server:  legacy.GetString("postgres.ldap_server"),
					Prefix:  legacy.GetString("postgres.ldap_prefix"),
					Suffix:  legacy.GetString("postgres.ldap_suffix"),
					Port:    legacy.GetInt("postgres.ldap_port"),
					Secure:  legacy.GetBool("postgres.ldap_secure"),
				},
			},
		}
	} else {
		pgConfig = postgresConfig{
			postgresBase: postgresBase{
				Username: legacy.GetString("postgres.install.username"),
				Password: legacy.GetString("postgres.install.password"),
				Port:     legacy.GetInt("postgres.install.port"),
			},
			postgresExisting: postgresExisting{
				Host:          legacy.GetString("postgres.host"),
				PGDumpPath:    legacy.GetString("postgres.pg_dump_path"),
				PGRestorePath: legacy.GetString("postgres.pg_restore_path"),
			},
		}
	}
	return rootConfig{
		Installer: installerConfig{
			InstallRoot:   legacy.GetString("installRoot"),
			BindAddresses: legacy.GetString("host"),
			Certs: certConfig{
				Key:  legacy.GetString("server_key_path"),
				Cert: legacy.GetString("server_cert_path"),
			},
			ServiceUsername: legacy.GetString("service_username"),
			WaitForYBAReady: legacy.GetInt("wait_for_yba_ready_seconds"),
			Services:        services,
		},
		Platform: platformConfig{
			Port:                  legacy.GetInt("platform.port"),
			Hsts:                  legacy.GetBool("platform.hsts_enabled"),
			CustomHeaders:         legacy.GetStringSlice("platform.custom_headers"),
			DisabledTlsAlgorithms: legacy.GetStringSlice("platform.tls_disabled_algorithms"),
			KeyStorePassword:      legacy.GetString("platform.keyStorePassword"),
			AppSecret:             legacy.GetString("platform.appSecret"),
			RestartSeconds:        legacy.GetInt("platform.restartSeconds"),
			SupportOriginUrl:      legacy.GetString("platform.support_origin_url"),
			AdditionalConfig:      legacy.GetString("platform.additional_config"),
			Oauth: oauthConfig{
				Enabled:        legacy.GetBool("platform.useOauth"),
				SecurityType:   legacy.GetString("platform.ybSecurityType"),
				ClientId:       legacy.GetString("platform.ybOidcClientId"),
				ClientSecret:   legacy.GetString("platform.ybOidcSecret"),
				DiscoveryUri:   legacy.GetString("platform.ybOidcDiscoveryUri"),
				Url:            legacy.GetString("platform.ywUrl"),
				Scope:          legacy.GetString("platform.ybOidcScope"),
				EmailAttribute: legacy.GetString("platform.ybOidcEmailAttr"),
			},
			Proxy: proxy{
				Enabled:           legacy.GetBool("platform.proxy.enabled"),
				HttpUrl:           legacy.GetString("platform.proxy.http_proxy"),
				HttpsUrl:          legacy.GetString("platform.proxy.https_proxy"),
				NoProxy:           legacy.GetString("platform.proxy.no_proxy"),
				JavaNonProxyHosts: legacy.GetString("platform.proxy.java_non_proxy"),
			},
		},
		Postgres: pgConfig,
		Prometheus: prometheusConfig{
			Port:              legacy.GetInt("prometheus.port"),
			RestartSeconds:    legacy.GetInt("prometheus.restartSeconds"),
			ScrapeInterval:    legacy.GetString("prometheus.scrapeInterval"),
			ScrapeTimeout:     legacy.GetString("prometheus.scrapeTimeout"),
			MaxConcurrency:    legacy.GetInt("prometheus.maxConcurrency"),
			MaxSamples:        legacy.GetInt("prometheus.maxSamples"),
			Timeout:           legacy.GetString("prometheus.timeout"),
			RetentionTime:     legacy.GetString("prometheus.retentionTime"),
			EnableHttps:       legacy.GetBool("prometheus.enableHttps"),
			EnableAuth:        legacy.GetBool("prometheus.enableAuth"),
			AuthUsername:      legacy.GetString("prometheus.authUsername"),
			AuthPassword:      legacy.GetString("prometheus.authPassword"),
			OOMScoreAdjust:    legacy.GetInt("prometheus.oomScoreAdjust"),
			AllowedTLSCiphers: legacy.GetStringSlice("prometheus.allowedTLSCiphers"),
			RemoteWrite: remoteWriteConfig{
				Enabled: legacy.GetBool("prometheus.remoteWrite.enabled"),
				Configs: legacy.GetString("prometheus.remoteWrite.configs"),
			},
			ScrapeConfig: scrapeConfigs{
				NodeAgent: scrapeJobConfig{
					Scheme: legacy.GetString("prometheus.scrapeConfig.node-agent.scheme"),
				},
				Node: scrapeJobConfig{
					Scheme: legacy.GetString("prometheus.scrapeConfig.node.scheme"),
				},
				Yugabyte: scrapeJobConfig{
					Scheme: legacy.GetString("prometheus.scrapeConfig.yugabyte.scheme"),
				},
			},
		},
		PerfAdvisor: perfAdvisorConfig{
			Enabled:        legacy.GetBool("perfAdvisor.enabled"),
			Port:           legacy.GetInt("perfAdvisor.port"),
			RestartSeconds: legacy.GetInt("perfAdvisor.restartSeconds"),
			PaSecret:       legacy.GetString("perfAdvisor.paSecret"),
			Callhome: callhomeConfig{
				Enabled:     legacy.GetBool("perfAdvisor.callhome.enabled"),
				Environment: legacy.GetString("perfAdvisor.callhome.environment"),
			},
			Tls: tlsConfig{
				Enabled:      legacy.GetBool("perfAdvisor.tls.enabled"),
				SSLProtocols: legacy.GetString("perfAdvisor.tls.sslProtocols"),
				Hsts:         legacy.GetBool("perfAdvisor.tls.hsts"),
			},
		},
	}
}

func loadConfig() (*viper.Viper, error) {
	newConfig := viper.New()
	addDefaults(newConfig)
	newConfig.SetConfigName("config")
	newConfig.AddConfigPath(common.YbactlInstallDir())
	if err := newConfig.ReadInConfig(); err != nil {
		return nil, err
	}
	return newConfig, nil
}
