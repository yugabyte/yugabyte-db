/*
 * Copyright (c) YugabyteDB, Inc.
 */

package checks

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/spf13/viper"
)

func TestCheckCertHostnames(t *testing.T) {
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatalf("failed to generate key: %v", err)
	}

	dnsCert := testCertPEM(t, key, "yba.example.com", []string{"yba.example.com"}, nil)
	ipCert := testCertPEM(t, key, "", nil, []net.IP{net.ParseIP("10.0.0.5")})
	wildcardCert := testCertPEM(t, key, "", []string{"*.example.com"}, nil)
	multiCert := testCertPEM(t, key, "",
		[]string{"yba.example.com", "yba-alt.example.com"}, nil)
	otherCert := testCertPEM(t, key, "", []string{"other.example.com"}, nil)
	cnOnlyCert := testCertPEM(t, key, "yba.example.com", nil, nil)
	cnOnlyWildcardCert := testCertPEM(t, key, "*.example.com", nil, nil)
	cnOnlyIPCert := testCertPEM(t, key, "10.0.0.5", nil, nil)
	cnWithSANCert := testCertPEM(t, key, "yba.example.com", []string{"other.example.com"}, nil)
	noNamesCert := testCertPEM(t, key, "", nil, nil)
	keyPEM := pem.EncodeToMemory(
		&pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(key)})

	tests := []struct {
		name string
		// contents of the server cert file. A nil value means the file is not created at all.
		contents []byte
		hosts    []string
		want     Status
	}{
		{"matching dns san", dnsCert, []string{"yba.example.com"}, StatusPassed},
		{"matching ip san", ipCert, []string{"10.0.0.5"}, StatusPassed},
		{"wildcard san", wildcardCert, []string{"yba.example.com"}, StatusPassed},
		{"wildcard san does not cover apex", wildcardCert, []string{"example.com"},
			StatusCritical},
		{"all hosts covered", multiCert,
			[]string{"yba.example.com", "yba-alt.example.com"}, StatusPassed},
		{"one host of two not covered warns", multiCert,
			[]string{"yba.example.com", "10.0.0.5"}, StatusWarning},
		{"no host of several covered fails", multiCert,
			[]string{"10.0.0.5", "other.example.com"}, StatusCritical},
		{"dns san does not cover ip host", dnsCert, []string{"10.0.0.5"}, StatusCritical},
		{"common name only match passes", cnOnlyCert, []string{"yba.example.com"}, StatusPassed},
		{"common name only wildcard match passes", cnOnlyWildcardCert,
			[]string{"yba.example.com"}, StatusPassed},
		{"common name only ip match passes", cnOnlyIPCert, []string{"10.0.0.5"}, StatusPassed},
		{"common name only mismatch fails", cnOnlyCert, []string{"other.example.com"},
			StatusCritical},
		{"common name matches host the san misses", cnWithSANCert,
			[]string{"yba.example.com"}, StatusPassed},
		{"san and common name together cover all hosts", cnWithSANCert,
			[]string{"other.example.com", "yba.example.com"}, StatusPassed},
		{"host covered only by common name still warns for the rest", cnWithSANCert,
			[]string{"yba.example.com", "third.example.com"}, StatusWarning},
		{"neither san nor common name covers host", cnWithSANCert,
			[]string{"third.example.com"}, StatusCritical},
		{"cert without a san or common name passes", noNamesCert,
			[]string{"yba.example.com"}, StatusPassed},
		{"leaf of chain is verified", append(append([]byte{}, dnsCert...), otherCert...),
			[]string{"yba.example.com"}, StatusPassed},
		{"leaf of chain mismatch fails", append(append([]byte{}, otherCert...), dnsCert...),
			[]string{"yba.example.com"}, StatusCritical},
		{"private key block is skipped", append(append([]byte{}, keyPEM...), dnsCert...),
			[]string{"yba.example.com"}, StatusPassed},
		{"missing cert file fails", nil, []string{"yba.example.com"}, StatusCritical},
		{"non pem cert file fails", []byte("this is not a cert\n"),
			[]string{"yba.example.com"}, StatusCritical},
		{"pem file without a cert fails", keyPEM, []string{"yba.example.com"}, StatusCritical},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			certPath := filepath.Join(t.TempDir(), "server_cert.pem")
			if test.contents != nil {
				if err := os.WriteFile(certPath, test.contents, 0600); err != nil {
					t.Fatalf("failed to write cert file: %v", err)
				}
			}

			status, err := checkCertHostnames(certPath, test.hosts)
			if status != test.want {
				t.Errorf("expected status %s, got %s (err: %v)", test.want, status, err)
			}
			if test.want == StatusPassed && err != nil {
				t.Errorf("expected no error on pass, got %v", err)
			}
			if test.want != StatusPassed && err == nil {
				t.Errorf("expected an error with status %s, got none", test.want)
			}
		})
	}
}

// The error is what preflight logs as the warning, so it has to name the hosts the cert misses
// rather than every host that was checked.
func TestCheckCertHostnamesErrorNamesOnlyUnmatchedHosts(t *testing.T) {
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatalf("failed to generate key: %v", err)
	}
	certPath := filepath.Join(t.TempDir(), "server_cert.pem")
	contents := testCertPEM(t, key, "", []string{"yba.example.com"}, nil)
	if err := os.WriteFile(certPath, contents, 0600); err != nil {
		t.Fatalf("failed to write cert file: %v", err)
	}

	status, err := checkCertHostnames(certPath, []string{"yba.example.com", "10.0.0.5"})
	if status != StatusWarning {
		t.Fatalf("expected status %s, got %s (err: %v)", StatusWarning, status, err)
	}
	if !strings.Contains(err.Error(), "does not cover host 10.0.0.5,") {
		t.Errorf("expected the error to name only the unmatched host 10.0.0.5, got %v", err)
	}
}

// A self signed cert is generated from the configured host, so there is nothing to verify.
func TestServerCertHostnameSkippedForSelfSignedCert(t *testing.T) {
	origCertPath := viper.GetString("server_cert_path")
	defer viper.Set("server_cert_path", origCertPath)
	viper.Set("server_cert_path", "")

	res := ServerCertHostname.Execute()
	if res.Status != StatusPassed {
		t.Errorf("expected status %s, got %s (err: %v)", StatusPassed, res.Status, res.Error)
	}
}

// testCertPEM creates a self signed cert with the given common name and subject alternative names,
// returning its pem encoding.
func testCertPEM(
	t *testing.T,
	key *rsa.PrivateKey,
	commonName string,
	dnsNames []string,
	ips []net.IP) []byte {

	t.Helper()
	template := &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: commonName},
		NotBefore:    time.Now(),
		NotAfter:     time.Now().Add(24 * time.Hour),
		DNSNames:     dnsNames,
		IPAddresses:  ips,
	}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatalf("failed to create cert: %v", err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
}
