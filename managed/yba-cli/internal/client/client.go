/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strings"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// AuthAPIClient is a auth YBA Client

var cliVersion = "0.1.0"
var hostVersion = "0.1.0"

// AuthAPIClient contains authenticated api client and customer UUID
type AuthAPIClient struct {
	RestClient   *RestAPIClient
	APIClient    *ybaclient.APIClient
	APIv2Client  *ybav2client.APIClient
	CustomerUUID string
	ctx          context.Context
	stop         context.CancelFunc
}

// RestAPIClient contains http client
type RestAPIClient struct {
	Client *http.Client
	Host   string
	Scheme string
}

// SetVersion assigns the version of YBA CLI
func SetVersion(version string) {
	cliVersion = version
}

// GetVersion fetches the version of YBA CLI
func GetVersion() string {
	return cliVersion
}

// SetHostVersion assigns the version of YBA Host
func SetHostVersion(version string) {
	hostVersion = version
}

// GetHostVersion fetches the version of YBA Host
func GetHostVersion() string {
	return hostVersion
}

// NewAuthAPIClient function is returning a new AuthAPIClient Client
func NewAuthAPIClient() (*AuthAPIClient, error) {
	host := viper.GetString("host")
	// If the host is empty, then tell the user to run the auth command.
	// Need to check if current instance has a Yugabyte Anywhere installation.
	if len(host) == 0 {
		logrus.Fatalln(
			formatter.Colorize(
				"No valid YugabyteDB Anywhere Host detected. "+
					"Run \"yba auth\" or \"yba login\" to authenticate "+
					"with YugabyteDB Anywhere or run the command with -H flag.\n",
				formatter.RedColor))
	}
	url, err := ParseURL(host)
	if err != nil {
		return nil, err
	}

	apiToken := viper.GetString("apiToken")
	// If the api token is empty, then tell the user to run the auth command.
	if len(apiToken) == 0 {
		logrus.Fatalln(
			formatter.Colorize(
				fmt.Sprintf(
					"No valid API token detected for YugabyteDB Anywhere on %s. "+
						"Run \"yba auth\" or \"yba login\" to "+
						"authenticate with YugabyteDB Anywhere or run the command with -a flag.\n",
					host),
				formatter.RedColor))
	}

	return NewAuthAPIClientInitialize(url, apiToken)
}

// NewAuthAPIClientInitialize function is returning a new AuthAPIClient Client
func NewAuthAPIClientInitialize(url *url.URL, apiToken string) (*AuthAPIClient, error) {

	cfg := ybaclient.NewConfiguration()
	restAPIClient := &RestAPIClient{
		Client: &http.Client{Timeout: 30 * time.Second},
		Host:   url.Host,
	}
	cfg.Host = url.Host
	cfg.Scheme = url.Scheme
	if url.Scheme == util.HTTPSURLScheme {
		cfg.Scheme = util.HTTPSURLScheme
		restAPIClient.Scheme = util.HTTPSURLScheme
		tr, err := getConnectionTransport()
		if err != nil {
			return nil, err
		}
		cfg.HTTPClient = &http.Client{Transport: tr}
		restAPIClient.Client.Transport = tr
	} else {
		if !viper.GetBool("insecure") {
			errMessage := "Invalid or missing value provided for 'insecure'. Setting it to 'true'.\n"
			logrus.Error(formatter.Colorize(errMessage, formatter.YellowColor))
			viper.Set("insecure", true)
		}
		cfg.Scheme = util.HTTPURLScheme
		restAPIClient.Scheme = util.HTTPURLScheme
	}
	cfg.DefaultHeader = map[string]string{
		"X-AUTH-YW-API-TOKEN": apiToken,
	}

	cfgV2 := ybav2client.NewConfiguration()
	cfgV2.Host = url.Host
	cfgV2.Scheme = url.Scheme
	if url.Scheme == util.HTTPSURLScheme {
		cfgV2.Scheme = util.HTTPSURLScheme
		tr, err := getConnectionTransport()
		if err != nil {
			return nil, err
		}
		cfgV2.HTTPClient = &http.Client{Transport: tr}
	} else {
		if !viper.GetBool("insecure") {
			errMessage := "Invalid or missing value provided for 'insecure'. Setting it to 'true'.\n"
			logrus.Error(formatter.Colorize(errMessage, formatter.YellowColor))
			viper.Set("insecure", true)
		}
		cfgV2.Scheme = util.HTTPURLScheme
	}
	cfgV2.DefaultHeader = map[string]string{
		"X-AUTH-YW-API-TOKEN": apiToken,
	}

	apiClient := ybaclient.NewAPIClient(cfg)
	apiV2Client := ybav2client.NewAPIClient(cfgV2)

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)

	return &AuthAPIClient{
		restAPIClient,
		apiClient,
		apiV2Client,
		"",
		ctx,
		stop,
	}, nil
}

// NewAuthAPIClientAndCustomer before every command to access YBA host
func NewAuthAPIClientAndCustomer() *AuthAPIClient {
	authAPI, err := NewAuthAPIClient()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	authAPI.IsCLISupported()
	err = authAPI.GetCustomerUUID()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	return authAPI
}

// ParseURL returns a URL if string is valid, or returns error
func ParseURL(host string) (*url.URL, error) {
	if strings.HasPrefix(strings.ToLower(host), "http://") {
		warning := formatter.Colorize(
			fmt.Sprintf("You are using insecure api endpoint %s\n", host),
			formatter.YellowColor,
		)
		logrus.Debugf(warning)
	} else if !strings.HasPrefix(strings.ToLower(host), "https://") {
		host = "https://" + host
	}

	endpoint, err := url.ParseRequestURI(host)
	if err != nil {
		return nil, fmt.Errorf("could not parse YBA url (%s): %w", host, err)
	}
	return endpoint, err
}

// YBAMinimumVersion contains the minimum YBA version for stable and preview releases for a feature
type YBAMinimumVersion struct {
	Stable  string
	Preview string
}

// CheckValidYBAVersion allows operation if version is higher than listed versions
// For releases older than 2024.1, keeping both stable and preview min version as the same
// version would provide the correct result
// For features on and after 2024.1, min stable and min preview must be different
func (a *AuthAPIClient) CheckValidYBAVersion(versions YBAMinimumVersion) (bool,
	string, error) {

	r, _, err := a.GetAppVersion().Execute()
	if err != nil {
		host := viper.GetString("host")
		return false, "", fmt.Errorf("YugabyteDB Anywhere is not available at host %s", host)
	}
	currentVersion := r["version"]
	// check if current version is stable or preview
	// if stable, check with stable release, else with preview release
	var v string
	if util.IsVersionStable(currentVersion) {
		v = versions.Stable
	} else {
		v = versions.Preview
	}
	check, err := util.CompareYbVersions(currentVersion, v)
	if err != nil {
		return false, "", err
	}
	if check == 0 || check == 1 {
		return true, currentVersion, err
	}

	return false, currentVersion, err
}

// IsCLISupported checks if the CLI version is supported
func (a *AuthAPIClient) IsCLISupported() {
	allowedVersions := YBAMinimumVersion{
		Stable:  util.MinCLIStableVersion,
		Preview: util.MinCLIPreviewVersion,
	}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	if !allowed {
		errMessage := fmt.Sprintf(
			"YugabyteDB Anywhere CLI is not supported for YugabyteDB Anywhere Host version %s. "+
				"Please use a version greater than or equal to Stable: %s, Preview: %s\n",
			version,
			allowedVersions.Stable,
			allowedVersions.Preview)
		logrus.Fatalln(formatter.Colorize(errMessage, formatter.RedColor))
	}

	SetHostVersion(version)
}

// CheckValidYBAVersionForCommand checks if the command is supported for the current YBA version
func (a *AuthAPIClient) CheckValidYBAVersionForCommand(
	cmd string,
	stableVersion string,
	previewVersion string,
) {
	minVersion := YBAMinimumVersion{
		Stable:  stableVersion,
		Preview: previewVersion,
	}
	allowed, version, err := a.CheckValidYBAVersion(minVersion)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	if !allowed {
		errMessage := fmt.Sprintf(
			"Command '%s' is not supported for YugabyteDB Anywhere Host version %s. "+
				"Please use a version greater than or equal to Stable: %s, Preview: %s\n",
			cmd,
			version,
			minVersion.Stable,
			minVersion.Preview)
		logrus.Fatalln(formatter.Colorize(errMessage, formatter.RedColor))
	}
}

func getConnectionTransport() (*http.Transport, error) {
	useInsecure := viper.GetBool("insecure")
	caCertPath := viper.GetString("ca-cert")
	var tr *http.Transport
	if useInsecure {
		tr = &http.Transport{TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}
		return tr, nil
	}
	if len(caCertPath) > 0 {
		caCert, err := os.ReadFile(caCertPath)
		if err != nil {
			return nil, err
		}
		caCertPool := x509.NewCertPool()
		if ok := caCertPool.AppendCertsFromPEM(caCert); !ok {
			return nil, fmt.Errorf("failed to append CA certificate from %s", caCertPath)
		}
		tr = &http.Transport{TLSClientConfig: &tls.Config{RootCAs: caCertPool}}
		return tr, nil
	}

	return nil, fmt.Errorf("CA certificate path cannot be empty when using secure connection")
}
