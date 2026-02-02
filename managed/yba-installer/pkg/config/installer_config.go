package config

func New() rootConfig {
	return rootConfig{}
}

// rootConfig is the root configuration for the installer. It contains all the
// sub-configurations for the installer, such as installer config (installRoot, etc.), platform,
// postgres, prometheus, and any other service we may eventually support.
type rootConfig struct {
	Installer   installerConfig   `mapstructure:"installer"`
	Platform    platformConfig    `mapstructure:"platform"`
	Prometheus  prometheusConfig  `mapstructure:"prometheus"`
	Postgres    postgresConfig    `mapstructure:"postgres"`
	PerfAdvisor perfAdvisorConfig `mapstructure:"perfAdvisor"`
}

type Service string

const (
	ServicePlatform           Service = "platform"
	ServicePostgres           Service = "postgres"
	ServicePrometheus         Service = "prometheus"
	ServicePerformanceAdvisor Service = "yb-perf-advisor"
)

func (s Service) String() string {
	return string(s)
}

func (s Service) Validate() bool {
	switch s {
	case ServicePlatform, ServicePostgres, ServicePrometheus, ServicePerformanceAdvisor:
		return true
	default:
		return false
	}
}

type installerConfig struct {
	InstallRoot     string     `mapstructure:"install_root"`
	Certs           certConfig `mapstructure:",squash"`
	BindAddresses   string     `mapstructure:"bind_address"`
	WaitForYBAReady int        `mapstructure:"wait_for_yba_ready_seconds"`
	ServiceUsername string     `mapstructure:"service_username"`
	Services        []Service  `mapstructure:"services"`
}

type certConfig struct {
	Key  string `mapstructure:"server_key_path"`
	Cert string `mapstructure:"server_cert_path"`
}

type platformConfig struct {
	// Port is the port on which the platform service will run.
	Port                  int         `mapstructure:"port"`
	Hsts                  bool        `mapstructure:"hsts_enabled"`
	CustomHeaders         []string    `mapstructure:"custom_headers"`
	DisabledTlsAlgorithms []string    `mapstructure:"tls_disabled_algorithms"`
	Oauth                 oauthConfig `mapstructure:"oauth"`
	KeyStorePassword      string      `mapstructure:"keystore_password"`
	AppSecret             string      `mapstructure:"app_secret"`
	RestartSeconds        int         `mapstructure:"restart_seconds"`
	Proxy                 proxy       `mapstructure:"proxy"`
	SupportOriginUrl      string      `mapstructure:"support_origin_url"`
	AdditionalConfig      string      `mapstructure:"additional_config"`
	TrustedProxies        []string    `mapstructure:"trusted_proxies"`
}

type oauthConfig struct {
	Enabled        bool   `mapstructure:"enabled"`
	SecurityType   string `mapstructure:"security_type"`
	ClientId       string `mapstructure:"oidc_client_id"`
	ClientSecret   string `mapstructure:"oidc_client_secret"`
	DiscoveryUri   string `mapstructure:"oidc_discovery_uri"`
	Scope          string `mapstructure:"oidc_scope"`
	Url            string `mapstructure:"yw_url"`
	EmailAttribute string `mapstructure:"oidc_email_attr"`
}

type proxy struct {
	Enabled           bool   `mapstructure:"enabled"`
	HttpUrl           string `mapstructure:"http_proxy_url"`
	HttpsUrl          string `mapstructure:"https_proxy_url"`
	NoProxy           string `mapstructure:"no_proxy"`
	JavaNonProxyHosts string `mapstructure:"java_non_proxy_hosts"`
}

type remoteWriteConfig struct {
	Enabled bool   `mapstructure:"enabled"`
	Configs string `mapstructure:"configs"`
}

type scrapeJobConfig struct {
	Scheme string `mapstructure:"scheme"`
}

type scrapeConfigs struct {
	NodeAgent scrapeJobConfig `mapstructure:"node-agent"`
	Node      scrapeJobConfig `mapstructure:"node"`
	Yugabyte  scrapeJobConfig `mapstructure:"yugabyte"`
}

type prometheusConfig struct {
	Port              int               `mapstructure:"port"`
	RestartSeconds    int               `mapstructure:"restart_seconds"`
	ScrapeInterval    string            `mapstructure:"scrape_interval"`
	ScrapeTimeout     string            `mapstructure:"scrape_timeout"`
	MaxConcurrency    int               `mapstructure:"max_concurrency"`
	MaxSamples        int               `mapstructure:"max_samples"`
	Timeout           string            `mapstructure:"timeout"`
	RetentionTime     string            `mapstructure:"retention_time"`
	EnableHttps       bool              `mapstructure:"enable_https"`
	EnableAuth        bool              `mapstructure:"enable_auth"`
	AuthUsername      string            `mapstructure:"auth_username"`
	AuthPassword      string            `mapstructure:"auth_password"`
	OOMScoreAdjust    int               `mapstructure:"oom_score_adjust"`
	AllowedTLSCiphers []string          `mapstructure:"allowed_tls_ciphers"`
	RemoteWrite       remoteWriteConfig `mapstructure:"remote_write"`
	ScrapeConfig      scrapeConfigs     `mapstructure:"scrape_config"`
}

type postgresConfig struct {
	postgresBase     `mapstructure:",squash"`
	postgresInstall  `mapstructure:",squash"`
	postgresExisting `mapstructure:",squash"`
}

type postgresBase struct {
	Port     int    `mapstructure:"port,squash"`
	Username string `mapstructure:"username,squash"`
	Password string `mapstructure:"password,squash"`
}

type postgresInstall struct {
	RestartSeconds int        `mapstructure:"restart_seconds"`
	LDAP           ldapConfig `mapstructure:"ldap"`
}

type postgresExisting struct {
	Host          string `mapstructure:"host"`
	PGDumpPath    string `mapstructure:"pg_dump_path"`
	PGRestorePath string `mapstructure:"pg_restore_path"`
}

type ldapConfig struct {
	Enabled bool   `mapstructure:"enabled"`
	Server  string `mapstructure:"server"`
	Port    int    `mapstructure:"port"`
	Prefix  string `mapstructure:"prefix"`
	Suffix  string `mapstructure:"suffix"`
	Secure  bool   `mapstructure:"secure"`
}

type perfAdvisorConfig struct {
	Enabled        bool           `mapstructure:"enabled"`
	Port           int            `mapstructure:"port"`
	RestartSeconds int            `mapstructure:"restart_seconds"`
	PaSecret       string         `mapstructure:"pa_secret"`
	Callhome       callhomeConfig `mapstructure:"callhome"`
	Tls            tlsConfig      `mapstructure:"tls"`
}

type tlsConfig struct {
	Enabled          bool   `mapstructure:"enabled"`
	SSLProtocols     string `mapstructure:"ssl_protocols"`
	Hsts             bool   `mapstructure:"hsts"`
	KeystorePassword string `mapstructure:"keystore_password"`
}

type callhomeConfig struct {
	Enabled     bool   `mapstructure:"enabled"`
	Environment string `mapstructure:"environment"`
}
