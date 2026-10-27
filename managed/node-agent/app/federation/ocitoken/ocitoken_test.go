// Copyright (c) YugabyteDB, Inc.

package ocitoken

import (
	"context"
	"encoding/base64"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"
	"unicode/utf8"
)

const (
	validJWT = "eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiJ4In0.c2ln"
	// A value that must never surface in an error: it stands for a credential or token.
	canary = "CANARY-SECRET-VALUE"
)

type fakeSigner struct {
	err    error
	signed int
}

func (s *fakeSigner) Sign(r *http.Request) error {
	s.signed++
	if s.err != nil {
		return s.err
	}
	r.Header.Set("Authorization", "Signature test")
	return nil
}

func exchange(t *testing.T, srv *httptest.Server, signer Signer) (string, error) {
	t.Helper()
	return Exchange(context.Background(), srv.Client(), signer, srv.URL+tokenPath)
}

func mustNoError(t *testing.T, err error) {
	t.Helper()
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
}

func mustError(t *testing.T, err error) string {
	t.Helper()
	if err == nil {
		t.Fatal("expected an error, got none")
	}
	return err.Error()
}

func checkContains(t *testing.T, got, want string) {
	t.Helper()
	if !strings.Contains(got, want) {
		t.Errorf("%q does not contain %q", got, want)
	}
}

func checkNotContains(t *testing.T, got, unwanted string) {
	t.Helper()
	if strings.Contains(got, unwanted) {
		t.Errorf("%q must not contain %q", got, unwanted)
	}
}

func TestValidateIdentityDomainURL(t *testing.T) {
	valid := []string{
		"https://idcs-abc123.identity.oraclecloud.com",
		// The OCI console shows the domain URL with the port.
		"https://idcs-abc123.identity.oraclecloud.com:443",
		"https://idcs-abc123.identity.oraclegovcloud.com",
	}
	for _, u := range valid {
		if err := ValidateIdentityDomainURL(u); err != nil {
			t.Errorf("%q should be valid: %v", u, err)
		}
	}
	invalid := []string{
		"",
		"idcs-abc123.identity.oraclecloud.com",
		"http://idcs-abc123.identity.oraclecloud.com",
		"https://idcs-abc123.identity.oraclecloud.com/",
		"https://idcs-abc123.identity.oraclecloud.com/oauth2/v1/token",
		"https://idcs-abc123.identity.oraclecloud.com:8443",
		"https://user@idcs-abc123.identity.oraclecloud.com",
		"https://idcs-abc123.identity.oraclecloud.com?x=1",
		"https://idcs-abc123.identity.oraclecloud.com evil.example",
		"https://localhost",
		"https://-bad.identity.oraclecloud.com",
		"https://" + strings.Repeat("a", 64) + ".oraclecloud.com",
	}
	for _, u := range invalid {
		if err := ValidateIdentityDomainURL(u); err == nil {
			t.Errorf("%q should be rejected", u)
		}
	}
}

func TestTokenEndpoint(t *testing.T) {
	endpoint, err := TokenEndpoint("https://idcs-abc123.identity.oraclecloud.com:443")
	mustNoError(t, err)
	want := "https://idcs-abc123.identity.oraclecloud.com:443/oauth2/v1/token"
	if endpoint != want {
		t.Errorf("endpoint = %q, want %q", endpoint, want)
	}
	if _, err := TokenEndpoint("https://idcs-abc123.identity.oraclecloud.com/"); err == nil {
		t.Error("a trailing slash must be rejected")
	}
}

func TestExchangeSendsSignedTokenExchangeRequest(t *testing.T) {
	var gotForm map[string]string
	var gotAuth, gotType, gotMethod, gotPath string
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if err := r.ParseForm(); err != nil {
			t.Errorf("parse form: %v", err)
		}
		gotForm = map[string]string{
			"grant_type":           r.PostForm.Get("grant_type"),
			"scope":                r.PostForm.Get("scope"),
			"requested_token_type": r.PostForm.Get("requested_token_type"),
		}
		gotAuth, gotType = r.Header.Get("Authorization"), r.Header.Get("Content-Type")
		gotMethod, gotPath = r.Method, r.URL.Path
		_, _ = w.Write([]byte(`{"access_token":"` + validJWT + `"}`))
	}))
	defer srv.Close()
	signer := &fakeSigner{}

	token, err := exchange(t, srv, signer)

	mustNoError(t, err)
	if token != validJWT {
		t.Errorf("token = %q, want %q", token, validJWT)
	}
	if signer.signed != 1 {
		t.Errorf("request signed %d times, want 1", signer.signed)
	}
	if gotMethod != http.MethodPost || gotPath != tokenPath {
		t.Errorf("request was %s %s, want POST %s", gotMethod, gotPath, tokenPath)
	}
	if gotAuth != "Signature test" {
		t.Errorf("the request must carry the signer's credentials, got %q", gotAuth)
	}
	if gotType != "application/x-www-form-urlencoded;charset=utf-8" {
		t.Errorf("content type = %q", gotType)
	}
	wantForm := map[string]string{
		"grant_type":           "urn:ietf:params:oauth:grant-type:token-exchange",
		"scope":                "urn:opc:idm:__myscopes__",
		"requested_token_type": "urn:ietf:params:oauth:token-type:access_token",
	}
	if !reflect.DeepEqual(gotForm, wantForm) {
		t.Errorf("form = %v, want %v", gotForm, wantForm)
	}
}

// serveBody answers every request with status and body.
func serveBody(t *testing.T, status int, body string) *httptest.Server {
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(status)
		_, _ = w.Write([]byte(body))
	}))
	t.Cleanup(srv.Close)
	return srv
}

func TestExchangeAcceptsTokenField(t *testing.T) {
	token, err := exchange(t, serveBody(t, http.StatusOK, `{"token":"`+validJWT+`"}`), &fakeSigner{})
	mustNoError(t, err)
	if token != validJWT {
		t.Errorf("token = %q, want %q", token, validJWT)
	}
}

// A failure carries the status and the OAuth error fields, sanitised and bounded, and nothing else
// from the body: the body may hold anything, including a token on a path we did not anticipate.
func TestExchangeErrorRelaysOnlyStatusAndOAuthFields(t *testing.T) {
	cases := []struct {
		name   string
		status int
		body   string
		want   []string
	}{
		{"oauth error", http.StatusUnauthorized,
			`{"error":"invalid_client","error_description":"bad credentials",` +
				`"access_token":"` + canary + `","secret":"` + canary + `"}`,
			[]string{"HTTP 401", "invalid_client"}},
		{"not json", http.StatusBadGateway, "<html>" + canary + "</html>", []string{"HTTP 502"}},
		{"multi-line and oversized", http.StatusBadRequest,
			`{"error":"x\nforged log line","error_description":"` + strings.Repeat("A", 5000) + `"}`,
			[]string{"HTTP 400"}},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := exchange(t, serveBody(t, c.status, c.body), &fakeSigner{})
			msg := mustError(t, err)
			for _, w := range c.want {
				checkContains(t, msg, w)
			}
			checkNotContains(t, msg, canary)
			checkNotContains(t, msg, "\n")
			if len(msg) >= 400 {
				t.Errorf("error detail is not bounded: %d bytes", len(msg))
			}
		})
	}
}

func TestErrorDetailNeverSplitsAMultiByteCharacter(t *testing.T) {
	// "€" is three bytes; shifting the filler by 0..2 puts the cut on a character boundary and then
	// inside a character at each of its two interior byte positions.
	for pad := 0; pad < 3; pad++ {
		desc := strings.Repeat("A", maxDetailBytes-3-pad) + strings.Repeat("€", 10)
		got := errorDetail(tokenResponse{ErrorDescription: desc})

		if !utf8.ValidString(got) {
			t.Errorf("pad=%d: detail is not valid UTF-8: %q", pad, got)
		}
		if len(strings.TrimPrefix(got, ": ")) > maxDetailBytes {
			t.Errorf("pad=%d: detail exceeds %d bytes: %d", pad, maxDetailBytes, len(got))
		}
	}
}

func TestExchangeRejectsUnusableTokens(t *testing.T) {
	bodies := map[string]string{
		"empty":          `{}`,
		"not a jwt":      `{"access_token":"not-a-jwt"}`,
		"has whitespace": `{"access_token":"aaa.bbb.ccc ddd"}`,
		"too large":      `{"access_token":"aaa.` + strings.Repeat("b", maxTokenBytes) + `.ccc"}`,
		"not json":       `<<<`,
	}
	for name, body := range bodies {
		t.Run(name, func(t *testing.T) {
			_, err := exchange(t, serveBody(t, http.StatusOK, body), &fakeSigner{})
			checkContains(t, mustError(t, err), "no usable token")
		})
	}
}

func TestExchangeSignerFailureSendsNothing(t *testing.T) {
	called := false
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		called = true
	}))
	defer srv.Close()

	_, err := exchange(t, srv, &fakeSigner{err: errors.New("no instance principal")})

	checkContains(t, mustError(t, err), "no instance principal")
	if called {
		t.Error("an unsigned request must never be sent")
	}
}

// The request is signed for one host; following a redirect would forward credentials elsewhere.
func TestExchangeDoesNotFollowRedirects(t *testing.T) {
	targetHit := false
	target := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		targetHit = true
		_, _ = w.Write([]byte(`{"access_token":"` + validJWT + `"}`))
	}))
	defer target.Close()
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Redirect(w, r, target.URL, http.StatusTemporaryRedirect)
	}))
	defer srv.Close()
	client := NewClient()
	client.Transport = srv.Client().Transport

	_, err := Exchange(context.Background(), client, &fakeSigner{}, srv.URL+tokenPath)

	checkContains(t, mustError(t, err), "HTTP 307")
	if targetHit {
		t.Error("the redirect target must not be contacted")
	}
}

func TestReadTokenFile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "token.jwt")

	_, err := ReadTokenFile(path)
	mustError(t, err)

	// Surrounding whitespace (a hand-edited file) is not part of the token.
	for _, content := range []string{validJWT, "\n" + validJWT + "\n"} {
		mustNoError(t, os.WriteFile(path, []byte(content), 0600))
		got, err := ReadTokenFile(path)
		mustNoError(t, err)
		if got != validJWT {
			t.Errorf("read %q, want %q", got, validJWT)
		}
	}

	mustNoError(t, os.WriteFile(path, []byte(strings.Repeat("a", maxTokenBytes+1)), 0600))
	_, err = ReadTokenFile(path)
	mustError(t, err)
}

// jwtWithPayload builds a token-shaped string whose payload is the given JSON.
func jwtWithPayload(payload string) string {
	enc := base64.RawURLEncoding
	return enc.EncodeToString([]byte(`{"alg":"RS256"}`)) + "." +
		enc.EncodeToString([]byte(payload)) + ".c2ln"
}

func TestRequireLifetime(t *testing.T) {
	now := time.Unix(1_800_000_000, 0)
	expIn := func(d time.Duration) string {
		return jwtWithPayload(fmt.Sprintf(`{"exp":%d}`, now.Add(d).Unix()))
	}

	mustNoError(t, RequireLifetime(expIn(time.Hour), 30*time.Minute, now))
	// Exactly at the minimum is enough.
	mustNoError(t, RequireLifetime(expIn(30*time.Minute), 30*time.Minute, now))

	msg := mustError(t, RequireLifetime(expIn(10*time.Minute), 30*time.Minute, now))
	checkContains(t, msg, "valid for 10m0s")
	checkContains(t, msg, "30m0s")
	checkContains(t, msg, "access token lifetime")

	// An already-expired token is the same failure, reported as negative time left.
	mustError(t, RequireLifetime(expIn(-time.Minute), 30*time.Minute, now))

	// RFC 7519 allows a fractional NumericDate, and some issuers send one.
	fractional := jwtWithPayload(fmt.Sprintf(`{"exp":%d.5}`, now.Add(time.Hour).Unix()))
	mustNoError(t, RequireLifetime(fractional, 30*time.Minute, now))
	mustError(t, RequireLifetime(fractional, 2*time.Hour, now))
}

// The token is parsed without trusting it, so anything malformed must be an error and never a panic.
func TestRequireLifetimeRejectsMalformedTokens(t *testing.T) {
	now := time.Unix(1_800_000_000, 0)
	cases := map[string]string{
		"empty":            "",
		"one segment":      "abc",
		"two segments":     "abc.def",
		"four segments":    "a.b.c.d",
		"bad base64":       "aaa.!!!.ccc",
		"payload not json": jwtWithPayload("not json"),
		"no exp":           jwtWithPayload(`{"sub":"x"}`),
		"zero exp":         jwtWithPayload(`{"exp":0}`),
		"exp as string":    jwtWithPayload(`{"exp":"soon"}`),
	}
	for name, token := range cases {
		t.Run(name, func(t *testing.T) {
			checkContains(t, mustError(t, RequireLifetime(token, time.Minute, now)), "lifetime")
		})
	}
}
