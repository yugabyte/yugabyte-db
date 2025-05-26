/*
 * Copyright (c) YugaByte, Inc.
 */

package oidc

import (
	"fmt"
	"net/url"
	"strings"
	"syscall"

	"github.com/pkg/browser"
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/auth"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"golang.org/x/term"
)

const (
	thirdPartyEndpoint     = "/api/third_party_login"
	showAPITokenQueryParam = "show_api_token=true"
	minStableVersion       = "2025.1.1.0"
	minPreviewVersion      = "2.27.0.0"
)

// oidcLoginCmd is used to login to YBA using OIDC authentication
var oidcLoginCmd = &cobra.Command{
	Use:     "login",
	Short:   "Login to YBA using OIDC authentication",
	Long:    "Login to YBA using OIDC authentication",
	Example: `yba oidc login`,
	Run: func(cmd *cobra.Command, args []string) {
		force := util.MustGetFlagBool(cmd, "force")
		baseURL := auth.ViperVariablesInAuth(cmd, force)
		showAPIToken := util.MustGetFlagBool(cmd, "show-api-token")
		validateSSOLoginCLISupport(baseURL)
		openBrowserForAuth(baseURL)
		apiToken := getTokenFromUserHidden()
		auth.InitializeAuthenticatedSession(baseURL, apiToken, showAPIToken)
	},
}

func init() {
	oidcLoginCmd.Flags().SortFlags = false
	oidcLoginCmd.Flags().
		Bool("show-api-token", false, "[Optional] Show the API token after authentication. (default false)")
	oidcLoginCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage. Provide the host (--host/-H)")
}

func openBrowserForAuth(baseURL *url.URL) {
	authURL := fmt.Sprintf(
		"%s%s?%s",
		baseURL.String(),
		thirdPartyEndpoint,
		showAPITokenQueryParam)

	logrus.Infof("Opening browser to authenticate...\n")

	if err := browser.OpenURL(authURL); err != nil {
		logrus.WithError(err).
			Warn(formatter.Colorize("Failed to open browser.\n", formatter.YellowColor))
		showBrowserFallbackInstructions(authURL)
	}
	logrus.Info("Please authenticate in the browser and copy the API token displayed.\n")
}

func showBrowserFallbackInstructions(authURL string) {
	logrus.Info(
		formatter.Colorize(
			"Please open the following URL in your browser to authenticate:\n\n  ",
			formatter.YellowColor,
		),
	)
	logrus.Info(formatter.Colorize(authURL+"\n\n", formatter.GreenColor))
}

func getTokenFromUserHidden() string {
	fmt.Print("Enter your API token: ")
	tokenBytes, err := term.ReadPassword(int(syscall.Stdin))
	if err != nil {
		logrus.Fatalf("Error reading token: %v\n", err)
	}
	fmt.Println()
	return strings.TrimSpace(string(tokenBytes))
}

func validateSSOLoginCLISupport(baseURL *url.URL) {
	authAPI, err := client.NewAuthAPIClientInitialize(baseURL, "")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	authAPI.CheckValidYBAVersionForCommand(
		"yba oidc login",
		minStableVersion,
		minPreviewVersion,
	)
}
