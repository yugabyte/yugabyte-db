package common

import (
	"testing"
	"time"
)

func TestCertificateGeneration(t *testing.T) {
	// generate a root CA cert and key
	caCert, caKey := generateCert("/tmp/ca_cert.pem", "/tmp/ca_key.pem", true, 10*365*24*time.Hour, "", nil, nil)

	_, err := RunBash("openssl", []string{"x509", "-in", "/tmp/ca_cert.pem", "-text", "-noout"})
	if err != nil {
		t.Fatalf("Failed to open certificate with openssl (is openssl installed?)")
	}

	_, err = RunBash("openssl", []string{"rsa", "-in", "/tmp/ca_key.pem", "-text", "-noout"})
	if err != nil {
		t.Fatalf("Failed to open key file with openssl (is openssl installed?)")
	}

	// generate a server cert and key signed by the above root CA
	generateCert("/tmp/server_cert.pem", "/tmp/server_key.pem", false, 4*365*24*time.Hour, "127.0.0.1", caCert, caKey)

	_, err = RunBash("openssl", []string{"x509", "-in", "/tmp/server_cert.pem", "-text", "-noout"})
	if err != nil {
		t.Fatalf("Failed to open certificate with openssl (is openssl installed?)")
	}

	_, err = RunBash("openssl", []string{"rsa", "-in", "/tmp/server_key.pem", "-text", "-noout"})
	if err != nil {
		t.Fatalf("Failed to open key file with openssl (is openssl installed?)")
	}

	_, err = RunBash("openssl", []string{"verify", "-CAfile", "/tmp/ca_cert.pem", "/tmp/server_cert.pem"})
	if err != nil {
		t.Fatalf("Failed to open key file with openssl (is openssl installed?)")
	}

}
