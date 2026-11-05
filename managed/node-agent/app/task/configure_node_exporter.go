// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"net"
	"net/http"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"strconv"
	"time"
)

const (
	nodeExporterService = "node_exporter.service"
	// RootNodeExporterUnit is the root-level unit that YBA versions before 2026.2 laid out. It runs
	// node_exporter as prometheus, which can't read the node certificate.
	RootNodeExporterUnit = "/etc/systemd/system/" + nodeExporterService
	// Must match web_config in the YNP node_exporter run.j2, which creates the file empty and
	// passes it to --web.config.file.
	nodeExporterWebConfigSubpath = "node_exporter/web.yml"

	schemeHTTP  = "http"
	schemeHTTPS = "https"
	schemeDown  = "down"
)

var (
	nodeExporterProbeTimeout   = 2 * time.Second
	nodeExporterSchemeWait     = 30 * time.Second
	nodeExporterSchemeInterval = time.Second
)

// ConfigureNodeExporter writes node_exporter's web config and makes the exporter serve the
// resulting scheme. exporter-toolkit re-reads web.yml and the certificate on every TLS handshake,
// so a certificate or key change needs no restart; only a switch between HTTP and HTTPS does,
// because the listener type is fixed when the process starts.
type ConfigureNodeExporter struct {
	param    *pb.ConfigureNodeExporterInput
	username string
	logOut   util.Buffer
}

func NewConfigureNodeExporterHandler(
	param *pb.ConfigureNodeExporterInput,
	username string,
) *ConfigureNodeExporter {
	return &ConfigureNodeExporter{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *ConfigureNodeExporter) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *ConfigureNodeExporter) String() string {
	return "Configure Node Exporter Task"
}

func (h *ConfigureNodeExporter) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	output, err := h.configure(ctx)
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		h.logOut.WriteLine(err.Error())
		return nil, err
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_ConfigureNodeExporterOutput{
			ConfigureNodeExporterOutput: output,
		},
	}, nil
}

func (h *ConfigureNodeExporter) configure(
	ctx context.Context,
) (*pb.ConfigureNodeExporterOutput, error) {
	ybHome := h.param.GetYbHomeDir()
	if ybHome == "" {
		return nil, errors.New("ybHomeDir is required")
	}
	port := h.param.GetNodeExporterPort()
	if port == 0 {
		return nil, errors.New("nodeExporterPort is required")
	}
	// Only the user-level unit reads a web config, and only a unit in the DB user's scope can be
	// restarted here without sudo.
	isUser, _, err := module.IsUserSystemd(h.username, nodeExporterService)
	if err != nil {
		return nil, err
	}
	if !isUser {
		return nil, fmt.Errorf(
			"%s is not a user-level unit of %s; re-provision the node to move node_exporter"+
				" beside the DB services",
			nodeExporterService,
			h.username,
		)
	}
	content, err := h.webConfig()
	if err != nil {
		return nil, err
	}
	webConfigPath := filepath.Join(ybHome, nodeExporterWebConfigSubpath)
	if err := util.WriteFileIfChanged(webConfigPath, content, 0640, h.username); err != nil {
		return nil, fmt.Errorf("failed to write %s: %w", webConfigPath, err)
	}

	want := schemeHTTP
	if h.param.GetTlsEnabled() {
		want = schemeHTTPS
	}
	addr := net.JoinHostPort("127.0.0.1", strconv.Itoa(int(port)))
	got := probeScheme(ctx, addr)
	h.logOut.WriteLine("node_exporter serves %s, want %s", got, want)
	restarted := false
	if got != want {
		if err := module.ControlSystemdService(
			ctx, h.username, nodeExporterService, "restart", h.logOut); err != nil {
			return nil, err
		}
		restarted = true
	}
	if err := waitForScheme(ctx, addr, want); err != nil {
		return nil, err
	}
	return &pb.ConfigureNodeExporterOutput{Scheme: want, Restarted: restarted}, nil
}

// webConfig returns the web.yml content; empty means plain HTTP.
func (h *ConfigureNodeExporter) webConfig() (string, error) {
	if !h.param.GetTlsEnabled() {
		return "", nil
	}
	certFile, keyFile := h.param.GetCertFile(), h.param.GetKeyFile()
	if certFile == "" || keyFile == "" {
		return "", errors.New("certFile and keyFile are required when TLS is enabled")
	}
	// node_exporter refuses to start when a configured file is missing, so fail before touching
	// web.yml rather than leave an exporter that cannot restart.
	for _, file := range []string{certFile, keyFile} {
		if _, err := os.Stat(file); err != nil {
			return "", fmt.Errorf("TLS file for node_exporter: %w", err)
		}
	}
	return fmt.Sprintf(
		"tls_server_config:\n  cert_file: %q\n  key_file: %q\n  min_version: TLS12\n",
		certFile,
		keyFile,
	), nil
}

// probeScheme reports what the listener at addr serves. An HTTPS server answers a plain request
// with 400, so only a 200 counts as HTTP.
func probeScheme(ctx context.Context, addr string) string {
	dialer := &net.Dialer{Timeout: nodeExporterProbeTimeout}
	// Only detects whether the listener speaks TLS; the certificate is not what is checked here.
	tlsConfig := &tls.Config{InsecureSkipVerify: true}
	if conn, err := tls.DialWithDialer(dialer, "tcp", addr, tlsConfig); err == nil {
		conn.Close()
		return schemeHTTPS
	}
	client := &http.Client{Timeout: nodeExporterProbeTimeout}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, "http://"+addr+"/", nil)
	if err != nil {
		return schemeDown
	}
	resp, err := client.Do(req)
	if err != nil {
		return schemeDown
	}
	resp.Body.Close()
	if resp.StatusCode == http.StatusOK {
		return schemeHTTP
	}
	return schemeDown
}

func waitForScheme(ctx context.Context, addr, want string) error {
	deadline := time.Now().Add(nodeExporterSchemeWait)
	for {
		got := probeScheme(ctx, addr)
		if got == want {
			return nil
		}
		if !time.Now().Before(deadline) {
			return fmt.Errorf(
				"node_exporter on %s serves %s after %s, want %s; check journalctl --user -u %s",
				addr,
				got,
				nodeExporterSchemeWait,
				want,
				nodeExporterService,
			)
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(nodeExporterSchemeInterval):
		}
	}
}
