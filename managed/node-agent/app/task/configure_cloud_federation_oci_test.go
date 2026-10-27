// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

const (
	testAudience = "//iam.googleapis.com/projects/1/locations/global/" +
		"workloadIdentityPools/p/providers/a"
	testDomain = "https://idcs-abc123.identity.oraclecloud.com"
	testHome   = "/home/yugabyte"
)

func ociHandler(audience, domain string) *ConfigureCloudFederation {
	h := &ConfigureCloudFederation{
		param: &pb.ConfigureCloudFederationInput{
			SourceCloud: pb.ConfigureCloudFederationInput_OCI,
			TargetCloud: pb.ConfigureCloudFederationInput_GCP,
			Config: &pb.ConfigureCloudFederationInput_Gcs{
				Gcs: &pb.GcsConfig{Audience: audience},
			},
		},
	}
	if domain != "" {
		h.param.OciSource = &pb.OciSourceConfig{IdentityDomainUrl: domain}
	}
	return h
}

func hashOf(t *testing.T, h *ConfigureCloudFederation, home string) string {
	t.Helper()
	s, err := h.desiredStateHash(home)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	return s
}

func TestValidateFederationDirection(t *testing.T) {
	const (
		aws  = pb.ConfigureCloudFederationInput_AWS
		gcp  = pb.ConfigureCloudFederationInput_GCP
		oci  = pb.ConfigureCloudFederationInput_OCI
		none = pb.ConfigureCloudFederationInput_CLOUD_PROVIDER_UNSPECIFIED
	)
	supported := [][2]pb.ConfigureCloudFederationInput_CloudProvider{
		{aws, gcp}, {gcp, aws}, {oci, gcp},
		// A YBA that predates the source field sends none; the target alone decides.
		{none, gcp}, {none, aws},
	}
	for _, r := range supported {
		if err := validateFederationDirection(r[0], r[1]); err != nil {
			t.Errorf("%s -> %s should be supported: %v", r[0], r[1], err)
		}
	}
	unsupported := [][2]pb.ConfigureCloudFederationInput_CloudProvider{
		{oci, aws},             // OCI to S3 has no flow
		{aws, oci}, {gcp, oci}, // nothing reaches OCI storage yet
		{oci, oci}, {aws, aws}, {gcp, gcp},
	}
	for _, r := range unsupported {
		if err := validateFederationDirection(r[0], r[1]); err == nil {
			t.Errorf("%s -> %s should be rejected", r[0], r[1])
		}
	}
}

// The AWS-source hash is what every already-federated node has stamped. If it drifted, the next
// node-agent upgrade would see every one of them as out of sync and restart YBC on all of them.
func TestAwsSourceGcsHashUnchangedByOciSupport(t *testing.T) {
	got := hashOf(t, gcsHandler(testAudience), testHome)
	sum := sha256.Sum256([]byte("v1|gcs|" + testHome + "|" + testAudience))
	if want := hex.EncodeToString(sum[:]); got != want {
		t.Errorf("AWS-source hash changed: got %s, want %s", got, want)
	}
}

func TestDesiredStateHashOciSource(t *testing.T) {
	base := hashOf(t, ociHandler(testAudience, testDomain), testHome)

	if again := hashOf(t, ociHandler(testAudience, testDomain), testHome); again != base {
		t.Errorf("hash not stable for identical inputs: %s vs %s", base, again)
	}
	different := map[string]string{
		"an AWS source with the same audience": hashOf(t, gcsHandler(testAudience), testHome),
		"another audience": hashOf(
			t,
			ociHandler(testAudience+"x", testDomain),
			testHome,
		),
		"another identity domain": hashOf(t,
			ociHandler(testAudience, "https://idcs-other.identity.oraclecloud.com"), testHome),
		"another home": hashOf(t, ociHandler(testAudience, testDomain), "/home/other"),
	}
	for what, h := range different {
		if h == base {
			t.Errorf("hash must change for %s", what)
		}
	}
}

func TestDesiredStateHashOciSourceRejectsBadInput(t *testing.T) {
	cases := map[string]*ConfigureCloudFederation{
		"no identity domain":      ociHandler(testAudience, ""),
		"an insecure domain":      ociHandler(testAudience, "http://insecure.example.com"),
		"a malformed audience":    ociHandler("not a valid audience!!", testDomain),
		"an empty audience":       ociHandler("", testDomain),
		"a source with no config": {param: &pb.ConfigureCloudFederationInput{}},
	}
	for name, h := range cases {
		if _, err := h.desiredStateHash(testHome); err == nil {
			t.Errorf("%s should be rejected", name)
		}
	}

	// OCI to S3 is not a supported direction: it must fail, not hash credentials that cannot work.
	h := &ConfigureCloudFederation{
		param: &pb.ConfigureCloudFederationInput{
			SourceCloud: pb.ConfigureCloudFederationInput_OCI,
			TargetCloud: pb.ConfigureCloudFederationInput_AWS,
			Config: &pb.ConfigureCloudFederationInput_S3{
				S3: &pb.S3Config{
					RoleArn:     "arn:aws:iam::123456789012:role/r",
					Audience:    testAudience,
					ProfileName: "p",
				},
			},
		},
	}
	_, err := h.desiredStateHash(testHome)
	if err == nil || !strings.Contains(err.Error(), "is not supported") {
		t.Errorf("OCI to S3 must be refused as unsupported, got %v", err)
	}
}

// systemd treats a quote, newline, % or $ in a unit as syntax, and these paths are interpolated
// into one, so anything outside a plain path must be refused.
func TestValidateUnitPath(t *testing.T) {
	valid := []string{
		"/home/yugabyte/node-agent/pkg/bin/node-agent",
		"/home/yugabyte/.yugabyte/federation/oci-identity-token.jwt",
		"/opt/yb_data+1/x@y",
	}
	for _, p := range valid {
		if err := validateUnitPath("path", p); err != nil {
			t.Errorf("%q should be accepted: %v", p, err)
		}
	}
	invalid := []string{
		"",
		"relative/path",
		`/home/y"ugabyte`,
		"/home/yugabyte\nExecStartPre=/bin/evil",
		"/home/yugabyte/%h",
		"/home/$USER/x",
		"/home/with space/x",
		"/home/a;b",
		"/home/a'b",
		"/home/a\\b",
	}
	for _, p := range invalid {
		if err := validateUnitPath("path", p); err == nil {
			t.Errorf("%q should be rejected", p)
		}
	}
}

func serverTemplate(t *testing.T, name string, values map[string]any) string {
	t.Helper()
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("cannot locate the test file")
	}
	path := filepath.Join(
		filepath.Dir(thisFile),
		"..",
		"..",
		"resources",
		"templates",
		"server",
		name,
	)
	out, err := module.ResolveTemplate(context.Background(), values, path)
	if err != nil {
		t.Fatalf("rendering %s: %v", name, err)
	}
	return out
}

func TestOciCredentialTemplate(t *testing.T) {
	tokenFile := "/home/yugabyte/.yugabyte/federation/oci-identity-token.jwt"
	out := serverTemplate(t, gcpFedCredsOciTemplate, map[string]any{
		"audience":   testAudience,
		"token_file": tokenFile,
	})

	var parsed map[string]any
	if err := json.Unmarshal([]byte(out), &parsed); err != nil {
		t.Fatalf("must render valid JSON: %v\n%s", err, out)
	}
	want := map[string]string{
		"type":               "external_account",
		"audience":           testAudience,
		"subject_token_type": "urn:ietf:params:oauth:token-type:jwt",
	}
	for k, v := range want {
		if parsed[k] != v {
			t.Errorf("%s = %v, want %s", k, parsed[k], v)
		}
	}
	source, ok := parsed["credential_source"].(map[string]any)
	if !ok || source["file"] != tokenFile {
		t.Errorf(
			"credential_source = %v, want a file source for %s",
			parsed["credential_source"],
			tokenFile,
		)
	}
	// None of the AWS source's metadata endpoints may leak into the OCI variant.
	for _, banned := range []string{"169.254.169.254", "aws"} {
		if strings.Contains(out, banned) {
			t.Errorf("the OCI credential must not mention %q:\n%s", banned, out)
		}
	}
}

func TestOciRefresherUnitTemplates(t *testing.T) {
	values := func(system bool) map[string]any {
		return map[string]any{
			"use_system_level_systemd": system,
			"user_name":                "yugabyte",
			"node_agent_bin":           "/home/yugabyte/node-agent/pkg/bin/node-agent",
			"identity_domain_url":      testDomain,
			"token_file":               "/home/yugabyte/.yugabyte/federation/oci-identity-token.jwt",
		}
	}
	contains := func(what, got, want string) {
		t.Helper()
		if !strings.Contains(got, want) {
			t.Errorf("%s does not contain %q:\n%s", what, want, got)
		}
	}

	user := serverTemplate(t, ociTokenServiceTemplate, values(false))
	if strings.Contains(user, "User=") {
		t.Errorf("a user-level unit already runs as the user:\n%s", user)
	}
	system := serverTemplate(t, ociTokenServiceTemplate, values(true))
	contains("system unit", system, "User=yugabyte")
	contains("system unit", system, "Group=yugabyte")

	wantExec := fmt.Sprintf(
		`ExecStart="/home/yugabyte/node-agent/pkg/bin/node-agent" federation oci-token `+
			`--identity-domain-url "%s" --output `+
			`"/home/yugabyte/.yugabyte/federation/oci-identity-token.jwt"`, testDomain)
	for what, unit := range map[string]string{"user unit": user, "system unit": system} {
		contains(what, unit, "Type=oneshot")
		contains(what, unit, wantExec)
	}

	timer := serverTemplate(t, ociTokenTimerTemplate, values(false))
	contains("timer", timer, "Unit="+ociTokenServiceName)
	contains("timer", timer, "OnUnitActiveSec=20min")
	contains("timer", timer, "WantedBy=timers.target")
}

// jwtExpiringAt builds a token-shaped string that expires at the given time.
func jwtExpiringAt(exp time.Time) string {
	enc := base64.RawURLEncoding.EncodeToString
	return enc([]byte(`{"alg":"RS256"}`)) + "." +
		enc([]byte(fmt.Sprintf(`{"exp":%d}`, exp.Unix()))) + "." + enc([]byte("sig"))
}

// A token the refresher stopped renewing must not look like a configured node: the timer has died
// or an outage outlasted the token, and only a re-apply repairs it. What decides is the token's own
// expiry, not how recently the file was written.
func TestOciTokenFresh(t *testing.T) {
	token := filepath.Join(t.TempDir(), ociTokenFileName)
	if ociTokenFresh(token) {
		t.Error("a missing token file must not count as fresh")
	}
	// The token's own expiry decides, not when the file was written: a recent mtime must not vouch
	// for an expired token, nor an old one condemn a token good for a day.
	for _, tc := range []struct {
		name    string
		content string
		age     time.Duration
		want    bool
	}{
		{"not a token", "not a token", 0, false},
		{"above the floor", jwtExpiringAt(time.Now().Add(ociTokenMinRemaining + time.Minute)), 0, true},
		{"below the floor", jwtExpiringAt(time.Now().Add(ociTokenMinRemaining - time.Minute)), 0, false},
		{"expired, just written", jwtExpiringAt(time.Now().Add(-time.Hour)), 0, false},
		{"long-lived, old file", jwtExpiringAt(time.Now().Add(24 * time.Hour)), 2 * time.Hour, true},
	} {
		if err := os.WriteFile(token, []byte(tc.content), 0600); err != nil {
			t.Fatal(err)
		}
		mtime := time.Now().Add(-tc.age)
		if err := os.Chtimes(token, mtime, mtime); err != nil {
			t.Fatal(err)
		}
		if got := ociTokenFresh(token); got != tc.want {
			t.Errorf("%s: fresh = %v, want %v", tc.name, got, tc.want)
		}
	}
}

// The timer, the setup guard and the freshness floor describe one schedule and only make sense
// together. The refresh interval lives in the timer template; this pins the rest against it.
func TestOciTokenTimingIsConsistent(t *testing.T) {
	const refreshInterval = 20 * time.Minute // OnUnitActiveSec in yb-federation-oci-token.timer.j2
	timer := serverTemplate(t, ociTokenTimerTemplate, map[string]any{})
	if !strings.Contains(timer, "OnUnitActiveSec=20min") {
		t.Fatalf("the timer no longer refreshes every 20 minutes; update this test:\n%s", timer)
	}
	if ociTokenMinLifetime <= refreshInterval {
		t.Errorf("a token must outlive the %s between refreshes, but the minimum is %s",
			refreshInterval, ociTokenMinLifetime)
	}
	// A healthy refresher, right before its next run, holds the shortest-lived token setup accepts
	// minus one interval. That must still count as fresh, or a node would be re-applied on every
	// reconcile.
	if ociTokenMinLifetime-refreshInterval <= ociTokenMinRemaining {
		t.Errorf("a healthy refresher would look stale: %s - %s <= %s",
			ociTokenMinLifetime, refreshInterval, ociTokenMinRemaining)
	}
}
