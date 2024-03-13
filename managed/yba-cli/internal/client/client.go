/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	"context"
	"crypto/tls"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// AuthAPIClient is a auth YBA Client

var cliVersion = "0.1.0"
var hostVersion = "0.1.0"

// AuthAPIClient contains authenticated api client and customer UUID
type AuthAPIClient struct {
	APIClient    *ybaclient.APIClient
	CustomerUUID string
	ctx          context.Context
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
				"No valid Host detected. Run `yba auth` to authenticate with YugabyteDB Anywhere.",
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
				"No valid API token detected. Run \"yba auth\" or \"yba login\" to "+
					"authenticate with YugabyteDB Anywhere or run the command with -a flag.",
				formatter.RedColor))
	}

	return NewAuthAPIClientInitialize(url, apiToken)
}

// NewAuthAPIClientInitialize function is returning a new AuthAPIClient Client
func NewAuthAPIClientInitialize(url *url.URL, apiToken string) (*AuthAPIClient, error) {

	cfg := ybaclient.NewConfiguration()
	//Configure the client

	cfg.Host = url.Host
	cfg.Scheme = url.Scheme
	if url.Scheme == "https" {
		cfg.Scheme = "https"
		tr := &http.Transport{TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}
		cfg.HTTPClient = &http.Client{Transport: tr}
	} else {
		cfg.Scheme = "http"
	}

	cfg.DefaultHeader = map[string]string{"X-AUTH-YW-API-TOKEN": apiToken}

	apiClient := ybaclient.NewAPIClient(cfg)

	ctx, _ := signal.NotifyContext(context.Background(), os.Interrupt)

	return &AuthAPIClient{
		apiClient,
		"",
		ctx,
	}, nil
}

// NewAuthAPIClientAndCustomer before every command to access YBA host
func NewAuthAPIClientAndCustomer() *AuthAPIClient {
	authAPI, err := NewAuthAPIClient()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	err = authAPI.GetCustomerUUID()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	return authAPI
}

// ParseURL returns a URL if string is valid, or returns error
func ParseURL(host string) (*url.URL, error) {
	if strings.HasPrefix(strings.ToLower(host), "http://") {
		logrus.Warnf("You are using insecure api endpoint %s\n", host)
	} else if !strings.HasPrefix(strings.ToLower(host), "https://") {
		host = "https://" + host
	}

	endpoint, err := url.ParseRequestURI(host)
	if err != nil {
		return nil, fmt.Errorf("could not parse YBA url (%s): %w", host, err)
	}
	return endpoint, err
}

// CheckValidYBAVersion allows operation if version is higher than listed versions
func (a *AuthAPIClient) CheckValidYBAVersion(versions []string) (bool,
	string, error) {

	r, response, err := a.GetAppVersion().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"YBA Version", "Get App Version")
		return false, "", errMessage
	}
	currentVersion := r["version"]
	for _, v := range versions {
		check, err := util.CompareYbVersions(currentVersion, v)
		if err != nil {
			return false, "", err
		}
		if check == 0 || check == 1 {
			return true, currentVersion, err
		}
	}
	return false, currentVersion, err
}
