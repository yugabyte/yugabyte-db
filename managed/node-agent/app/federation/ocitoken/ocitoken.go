// Copyright (c) YugabyteDB, Inc.

// Package ocitoken obtains the identity a node running on OCI presents to other clouds.
//
// OCI does not hand a node a token another cloud can verify: its native identity is an instance
// principal certificate plus request signing. The node therefore exchanges that principal at its
// OCI identity domain for a JWT, which GCP Workload Identity Federation accepts as the subject
// token of an external_account credential.
package ocitoken

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"node-agent/util"
	"os"
	"regexp"
	"strings"
	"time"
	"unicode/utf8"

	"github.com/oracle/oci-go-sdk/v65/common"
	"github.com/oracle/oci-go-sdk/v65/common/auth"
)

const (
	tokenPath = "/oauth2/v1/token"

	// The identity domain's token-exchange grant, asked to authenticate as the instance principal
	// and to return an access token (a JWT). This request shape - signed by the instance principal,
	// with no client credentials - is the one OpenAI documents for OCI workload identity federation.
	// Oracle's A-Team post "Federate OCI Workload Identity to Google Cloud" describes the whole
	// OCI-to-GCP flow. Neither has been run against a live identity domain, which is the first thing
	// to verify: that this request is accepted, and what iss, aud, sub and lifetime the token has.
	grantType          = "urn:ietf:params:oauth:grant-type:token-exchange"
	scope              = "urn:opc:idm:__myscopes__"
	requestedTokenType = "urn:ietf:params:oauth:token-type:access_token"

	requestTimeout = 20 * time.Second
	// Bounds on what is read back and written to disk, so a misbehaving endpoint cannot fill the
	// node's disk or memory.
	maxResponseBytes = 1 << 20
	maxTokenBytes    = 16 << 10
	// Longest error detail relayed from the endpoint.
	maxDetailBytes = 200
)

var (
	// An https URL with only a host, optionally with the :443 the OCI console shows. node-agent signs
	// the request with the node's own OCI credentials, so a path, query or userinfo is refused rather
	// than interpreted. Mirrors CrossCloudFederationTarget.isValidOciIdentityDomainUrl in YBA.
	identityDomainURLRegex = regexp.MustCompile(
		`^https://[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?` +
			`(\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+(:443)?$`)

	// A JWT is three base64url segments; the signature segment may be empty for an unsecured token,
	// which the target cloud would reject anyway.
	jwtRegex = regexp.MustCompile(`^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]*$`)
)

// Signer signs an outgoing request with the node's OCI credentials.
type Signer interface {
	Sign(r *http.Request) error
}

// ValidateIdentityDomainURL reports whether url names an identity domain we are willing to send a
// signed request to.
func ValidateIdentityDomainURL(identityDomainURL string) error {
	if len(identityDomainURL) > 300 || !identityDomainURLRegex.MatchString(identityDomainURL) {
		return errors.New(
			"identity domain URL must be an https URL with only a host, for example " +
				"https://idcs-<id>.identity.oraclecloud.com, with no path or trailing slash")
	}
	return nil
}

// TokenEndpoint returns the token-exchange endpoint of a validated identity domain.
func TokenEndpoint(identityDomainURL string) (string, error) {
	if err := ValidateIdentityDomainURL(identityDomainURL); err != nil {
		return "", err
	}
	return identityDomainURL + tokenPath, nil
}

// InstancePrincipalSigner signs requests as the instance principal of the node it runs on.
func InstancePrincipalSigner() (Signer, error) {
	provider, err := auth.InstancePrincipalConfigurationProvider()
	if err != nil {
		return nil, fmt.Errorf(
			"could not load the OCI instance principal (is this node an OCI instance?): %w", err)
	}
	return common.DefaultRequestSigner(provider), nil
}

// NewClient returns the HTTP client used for the exchange. It refuses redirects: the request is
// signed for one host, and following it elsewhere would only forward credentials.
func NewClient() *http.Client {
	return &http.Client{
		Timeout: requestTimeout,
		CheckRedirect: func(*http.Request, []*http.Request) error {
			return http.ErrUseLastResponse
		},
	}
}

type tokenResponse struct {
	AccessToken      string `json:"access_token"`
	Token            string `json:"token"`
	Error            string `json:"error"`
	ErrorDescription string `json:"error_description"`
}

// Exchange trades the node's instance principal for an identity-domain JWT at endpoint.
//
// The token and the response body are never put in an error: a failure carries the HTTP status and
// the OAuth error fields only. Errors from here end up in the YBC and systemd logs, and from there
// in support bundles.
func Exchange(
	ctx context.Context,
	client *http.Client,
	signer Signer,
	endpoint string,
) (string, error) {
	form := url.Values{}
	form.Set("grant_type", grantType)
	form.Set("scope", scope)
	form.Set("requested_token_type", requestedTokenType)

	req, err := http.NewRequestWithContext(
		ctx, http.MethodPost, endpoint, strings.NewReader(form.Encode()))
	if err != nil {
		return "", fmt.Errorf("invalid token endpoint: %w", err)
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded;charset=utf-8")
	req.Header.Set("Accept", "application/json")
	// Signed last: the signature covers the headers and the body hash set above.
	if err := signer.Sign(req); err != nil {
		return "", fmt.Errorf("could not sign the token request: %w", err)
	}

	resp, err := client.Do(req)
	if err != nil {
		// A transport error names the URL, never the credentials.
		return "", fmt.Errorf("token request failed: %w", err)
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(io.LimitReader(resp.Body, maxResponseBytes))
	if err != nil {
		return "", fmt.Errorf("could not read the token response: %w", err)
	}

	var parsed tokenResponse
	// A body that is not JSON is treated as empty; the status code still says what happened.
	_ = json.Unmarshal(body, &parsed)

	if resp.StatusCode < 200 || resp.StatusCode > 299 {
		return "", fmt.Errorf(
			"identity domain returned HTTP %d%s", resp.StatusCode, errorDetail(parsed))
	}
	token := parsed.AccessToken
	if token == "" {
		token = parsed.Token
	}
	if len(token) == 0 || len(token) > maxTokenBytes || !jwtRegex.MatchString(token) {
		return "", errors.New("identity domain returned no usable token")
	}
	return token, nil
}

// errorDetail renders the OAuth error fields, sanitised and truncated, for an error message.
func errorDetail(r tokenResponse) string {
	detail := strings.TrimSpace(r.Error + " " + r.ErrorDescription)
	if detail == "" {
		return ""
	}
	detail = strings.Map(func(c rune) rune {
		if c < 0x20 || c == 0x7f {
			return ' '
		}
		return c
	}, detail)
	if len(detail) > maxDetailBytes {
		// Cut on a rune boundary so a multi-byte character is dropped whole, never split in two.
		cut := maxDetailBytes
		for cut > 0 && !utf8.RuneStart(detail[cut]) {
			cut--
		}
		detail = detail[:cut]
	}
	return ": " + detail
}

// RequireLifetime returns an error when token will expire within min of now. A token that does not
// outlive the gap between refreshes would leave YBC with an expired credential for part of every
// cycle. Setup applies it to the first token; after that the token file's own expiry, checked the
// same way, says whether the refresher is keeping up.
//
// The expiry is read without verifying the signature: the token came from the endpoint we called
// (or from the file the refresher wrote), and the value only decides whether to proceed.
func RequireLifetime(token string, min time.Duration, now time.Time) error {
	claims, err := util.ExtractClaims(context.Background(), nil, token)
	if err != nil {
		return errors.New("could not read the token's lifetime: not a JWT")
	}
	exp, err := claims.GetExpirationTime()
	if err != nil || exp == nil {
		return errors.New("could not read the token's lifetime: no exp claim")
	}
	if lifetime := exp.Sub(now); lifetime < min {
		return fmt.Errorf(
			"the identity domain issued a token valid for %s, shorter than the %s needed between "+
				"refreshes; raise the access token lifetime of the identity domain",
			lifetime.Round(time.Second), min)
	}
	return nil
}

// ReadTokenFile returns the token the refresher stored at path. The read is bounded by the size of
// a token Exchange would accept, so a file that is not one cannot be pulled into memory.
func ReadTokenFile(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	data, err := io.ReadAll(io.LimitReader(f, maxTokenBytes+1))
	if err != nil {
		return "", err
	}
	if len(data) > maxTokenBytes {
		return "", errors.New("the token file is larger than any token the identity domain issues")
	}
	return strings.TrimSpace(string(data)), nil
}
