// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"net"
	"net/http"
	"net/http/httptest"
	pb "node-agent/generated/service"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestProbeScheme(t *testing.T) {
	ok := http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {})
	plain := httptest.NewServer(ok)
	defer plain.Close()
	secure := httptest.NewTLSServer(ok)
	defer secure.Close()
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	closedAddr := listener.Addr().String()
	listener.Close()

	ctx := context.Background()
	for _, tc := range []struct {
		name, addr, want string
	}{
		{"plain", plain.Listener.Addr().String(), schemeHTTP},
		{"tls", secure.Listener.Addr().String(), schemeHTTPS},
		{"nothing listening", closedAddr, schemeDown},
	} {
		if got := probeScheme(ctx, tc.addr); got != tc.want {
			t.Errorf("%s: probeScheme = %s, want %s", tc.name, got, tc.want)
		}
	}
}

func TestProbeSchemeRejectsNon200(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusBadRequest)
	}))
	defer server.Close()
	if got := probeScheme(context.Background(), server.Listener.Addr().String()); got != schemeDown {
		t.Errorf("probeScheme = %s, want %s", got, schemeDown)
	}
}

func TestWaitForSchemeTimesOut(t *testing.T) {
	savedWait, savedInterval := nodeExporterSchemeWait, nodeExporterSchemeInterval
	nodeExporterSchemeWait, nodeExporterSchemeInterval = 100*time.Millisecond, 20*time.Millisecond
	defer func() { nodeExporterSchemeWait, nodeExporterSchemeInterval = savedWait, savedInterval }()
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {}))
	defer server.Close()
	err := waitForScheme(context.Background(), server.Listener.Addr().String(), schemeHTTPS)
	if err == nil || !strings.Contains(err.Error(), "serves http") {
		t.Fatalf("waitForScheme error = %v, want one naming the served scheme", err)
	}
}

func TestNodeExporterWebConfig(t *testing.T) {
	dir := t.TempDir()
	cert := filepath.Join(dir, "node.10.0.0.1.crt")
	key := filepath.Join(dir, "node.10.0.0.1.key")
	for _, f := range []string{cert, key} {
		if err := os.WriteFile(f, []byte("x"), 0400); err != nil {
			t.Fatal(err)
		}
	}

	plain := &ConfigureNodeExporter{param: &pb.ConfigureNodeExporterInput{}}
	if got, err := plain.webConfig(); err != nil || got != "" {
		t.Errorf("TLS off: webConfig = %q, %v; want empty", got, err)
	}

	secure := &ConfigureNodeExporter{param: &pb.ConfigureNodeExporterInput{
		TlsEnabled: true, CertFile: cert, KeyFile: key,
	}}
	got, err := secure.webConfig()
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{
		"tls_server_config:\n", `cert_file: "` + cert + `"`, `key_file: "` + key + `"`,
		"min_version: TLS12",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("webConfig missing %q:\n%s", want, got)
		}
	}

	missing := &ConfigureNodeExporter{param: &pb.ConfigureNodeExporterInput{
		TlsEnabled: true, CertFile: filepath.Join(dir, "absent.crt"), KeyFile: key,
	}}
	if _, err := missing.webConfig(); err == nil {
		t.Error("webConfig accepted a missing certificate")
	}
}
