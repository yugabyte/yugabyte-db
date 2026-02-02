// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Generate a self-signed X.509 certificate for a TLS server. Outputs to
// 'cert.pem' and 'key.pem' and will overwrite existing files.

package common

import (
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"golang.org/x/crypto/ssh"
)

const (
	caCertTimeout     time.Duration = 10 * 365 * 24 * time.Hour // about 10 years
	serverCertTimeout time.Duration = 4 * 365 * 24 * time.Hour  // about 4 years
)

// Allow integrationtests.testutils.cert to create its own certs
var SelfSignedOrg string = "Yugabyte Self-Signed CA"

type ServerCertPaths struct {
	KeyPath  string
	CertPath string
}

func publicKey(priv any) any {
	switch k := priv.(type) {
	case *rsa.PrivateKey:
		return &k.PublicKey
	case *ecdsa.PrivateKey:
		return &k.PublicKey
	case ed25519.PrivateKey:
		return k.Public().(ed25519.PublicKey)
	default:
		return nil
	}
}

func generateSelfSignedServerCert(certPath string, keyPath string, caCertPath string, caKeyPath string, host string) {
	// generate a root CA cert and key
	caCert, caKey := generateCert(
		caCertPath, caKeyPath, true, /*isCA*/
		caCertTimeout, "", nil, nil)

	// generate a server cert and key signed by the above root CA
	generateCert(certPath, keyPath, false, /*isCA*/
		serverCertTimeout, host, caCert, caKey)

}

// Sourced from https://go.dev/src/crypto/tls/generate_cert.go
// which is part of the official crypto/tls package
func generateCert(
	certPath string,
	keyPath string,
	isCA bool,
	validFor time.Duration,
	host string,
	CAcert *x509.Certificate,
	CAkey *rsa.PrivateKey) (resultCert *x509.Certificate, resultKey *rsa.PrivateKey) {

	resultKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		log.Fatal(fmt.Sprintf("Failed to generate private key: %v.", err))
	}

	keyUsage := x509.KeyUsageDigitalSignature | x509.KeyUsageKeyEncipherment
	if isCA {
		keyUsage |= x509.KeyUsageCertSign
	}

	notBefore := time.Now()
	notAfter := notBefore.Add(validFor)

	serialNumberLimit := new(big.Int).Lsh(big.NewInt(1), 128)
	serialNumber, err := rand.Int(rand.Reader, serialNumberLimit)
	if err != nil {
		log.Fatal(fmt.Sprintf("Failed to generate serial number: %v.", err))
	}

	resultCert = &x509.Certificate{
		SerialNumber: serialNumber,
		NotBefore:    notBefore,
		NotAfter:     notAfter,
		KeyUsage:     keyUsage,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		IsCA:         isCA,
	}
	if isCA {
		resultCert.Subject.Organization = []string{SelfSignedOrg}
		resultCert.BasicConstraintsValid = true
	} else {
		hosts := strings.Split(host, ",")
		for _, h := range hosts {
			if ip := net.ParseIP(h); ip != nil {
				resultCert.IPAddresses = append(resultCert.IPAddresses, ip)
			} else {
				resultCert.DNSNames = append(resultCert.DNSNames, h)
			}
		}
	}

	var issuer *x509.Certificate
	var issuerKey *rsa.PrivateKey
	if isCA {
		issuer = resultCert
		issuerKey = resultKey
	} else {
		issuer = CAcert
		issuerKey = CAkey
	}
	derBytes, err := x509.CreateCertificate(rand.Reader, resultCert, issuer, publicKey(resultKey), issuerKey)
	if err != nil {
		log.Fatal(fmt.Sprintf("Failed to create certificate: %v.", err))
	}

	certOut, err := os.Create(certPath)
	if err != nil {
		log.Fatal(fmt.Sprintf("Failed to open cert.pem for writing: %v.", err))
	}
	defer certOut.Close()
	if err := pem.Encode(certOut, &pem.Block{Type: "CERTIFICATE", Bytes: derBytes}); err != nil {
		log.Fatal(fmt.Sprintf("Failed to write data to cert.pem: %v.", err))
	}

	keyOut, err := os.OpenFile(keyPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
	if err != nil {
		log.Fatal(fmt.Sprintf("Failed to open key.pem for writing: %v.", err))
	}
	defer keyOut.Close()
	privBytes := x509.MarshalPKCS1PrivateKey(resultKey)
	if err != nil {
		log.Fatal(fmt.Sprintf("Unable to marshal private key: %v.", err))
	}
	if err := pem.Encode(keyOut, &pem.Block{Type: "RSA PRIVATE KEY", Bytes: privBytes}); err != nil {
		log.Fatal(fmt.Sprintf("Failed to write data to key.pem: %v.", err))
	}

	log.Debug("Generated cert/key pair at " + certPath + " and " + keyPath)
	return resultCert, resultKey

}

func parseCertFromPem(filePath string) (*x509.Certificate, error) {
	certData, err := os.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to read certfile %s: %w", filePath, err)
	}

	block, rest := pem.Decode(certData)
	if len(rest) != 0 {
		return nil, fmt.Errorf("pem file with multiple blocks found")
	}
	return x509.ParseCertificate(block.Bytes)
}

// RetrievePrivateKey loads the key from the given file
func parsePrivateKey(filePath string) (*rsa.PrivateKey, error) {
	data, err := os.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to read private key file %s: %w", filePath, err)
	}
	//blocks, _ := pem.Decode(data)
	key, err := ssh.ParseRawPrivateKey(data)
	if err != nil {
		return nil, fmt.Errorf("failed to parse private key file %s: %w", filePath, err)
	}
	privateKey, ok := key.(*rsa.PrivateKey)
	if !ok {
		return nil, fmt.Errorf("could not convert private key to rsa.Privatekey")
	}
	return privateKey, nil
}

// GeneratePerfAdvisorTLSKeystore creates a PKCS12 keystore (tls.p12) from the given PEM cert
// and key, for use by Perf Advisor when TLS is enabled. outDir is the directory to write
// tls.p12 into (e.g. GetPerfAdvisorCertsDir()). password is the keystore password.
func GeneratePerfAdvisorTLSKeystore(certPath, keyPath, outDir, password string) error {
	if err := MkdirAll(outDir, DirMode); err != nil && !os.IsExist(err) {
		return fmt.Errorf("create perf-advisor certs dir: %w", err)
	}
	keystorePath := filepath.Join(outDir, "tls.p12")
	// openssl pkcs12 -export -out tls.p12 -inkey key.pem -in cert.pem -passout pass:<password>
	out := shell.Run("openssl", "pkcs12", "-export",
		"-out", keystorePath,
		"-inkey", keyPath,
		"-in", certPath,
		"-passout", "pass:"+password)
	if !out.Succeeded() {
		return fmt.Errorf("openssl pkcs12 export: %w", out.Error)
	}
	log.Debug("Generated Perf Advisor TLS keystore at " + keystorePath)
	if HasSudoAccess() {
		username := viper.GetString("service_username")
		if err := Chown(outDir, username, username, true); err != nil {
			return fmt.Errorf("chown perf-advisor certs dir: %w", err)
		}
	}
	return nil
}

// GetFirstCertInServerPem is mainly used to validate if the server.pem file is generated
// from self-signed certs or not. YBA Installer self-signed certs are generated to have exactly
// 1 cert and 1 private key in the server.pem file. We can assume the first cert we find in the
// pem is the cert we want.
func GetFirstCertInServerPem() (*x509.Certificate, error) {
	// Open the CA cert file
	serverPemPath := filepath.Join(GetSelfSignedCertsDir(), ServerPemPath)
	log.Debug("Reading server.pem file from " + serverPemPath)
	certData, err := os.ReadFile(serverPemPath)
	// handle not exists as no error
	if err != nil {
		return nil, fmt.Errorf("failed to open CA cert file %s: %w", serverPemPath, err)
	}

	block := &pem.Block{}
	for len(certData) > 0 {
		block, certData = pem.Decode(certData)
		if block == nil {
			return nil, fmt.Errorf("failed to decode PEM block")
		}
		if strings.Contains(strings.ToLower(block.Type), "private key") {
			continue
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			return nil, fmt.Errorf("failed to parse certificate: %w", err)
		}
		return cert, nil
	}
	return nil, fmt.Errorf("no cert found in pem file")
}
