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
		resultCert.Subject.CommonName = viper.GetString("host")
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

// BCFKSKeystoreName is the keystore the platform loads its TLS material from. BCFKS is the
// BouncyCastle FIPS keystore format: unlike PEM, which Play parses itself and re-wraps in a
// SunJSSE keystore, it is opened by the FIPS-validated provider. Generated whether or not FIPS
// mode is on, so the on-disk format does not change when the flag is flipped.
const BCFKSKeystoreName = "server.bcfks"

// keystorePasswordEnvVar is how openssl and keytool receive the keystore password, keeping it
// out of argv and therefore out of yba-ctl.log.
const keystorePasswordEnvVar = "YBA_KEYSTORE_PASSWORD"

// GenerateBCFKSKeystore converts a PEM cert and key into a BCFKS keystore at
// outDir/BCFKSKeystoreName, under the given alias and password.
func GenerateBCFKSKeystore(certPath, keyPath, outDir, alias, password string) error {
	return writeBCFKSKeystore(certPath, keyPath, filepath.Join(outDir, BCFKSKeystoreName),
		alias, password)
}

// writeBCFKSKeystore converts a PEM cert and key into a BCFKS keystore at keystorePath.
//
// keytool cannot read a PEM key directly, so this goes through a PKCS12 intermediate. That
// intermediate is written to a temporary directory and deleted, because openssl builds it with
// PBE algorithms that are not FIPS approved - nothing that is kept depends on them.
// keystoreToolOutput is what openssl or keytool actually said. Without it the failure reads as a
// bare "exit status 1", which says nothing about a rejected password, an unreadable provider jar
// or an unwritable path - and yba-ctl.log is the only other place to look.
func keystoreToolOutput(out *shell.Output) string {
	for _, s := range []string{out.StderrString(), out.StdoutString()} {
		if trimmed := strings.TrimSpace(s); trimmed != "" {
			return trimmed
		}
	}
	return "no output"
}

func writeBCFKSKeystore(certPath, keyPath, keystorePath, alias, password string) error {
	outDir := filepath.Dir(keystorePath)
	if err := MkdirAll(outDir, DirMode); err != nil && !os.IsExist(err) {
		return fmt.Errorf("create keystore dir: %w", err)
	}
	bcFipsJar, err := bcFipsJarPath()
	if err != nil {
		return err
	}
	tmpDir, err := os.MkdirTemp("", "yba-bcfks")
	if err != nil {
		return fmt.Errorf("create temp dir for keystore conversion: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	intermediate := filepath.Join(tmpDir, "intermediate.p12")
	// The password goes through the environment, not argv: shell.Run logs the joined argv to
	// yba-ctl.log, which the support bundle collects.
	passEnv := map[string]string{keystorePasswordEnvVar: password}
	out := shell.RunWithEnvVars("openssl", passEnv, "pkcs12", "-export",
		"-out", intermediate,
		"-inkey", keyPath,
		"-in", certPath,
		"-name", alias,
		"-passout", "env:"+keystorePasswordEnvVar)
	if !out.Succeeded() {
		return fmt.Errorf("openssl pkcs12 export: %w: %s", out.Error, keystoreToolOutput(out))
	}

	// keytool appends to an existing store, so it has to write somewhere empty - but not over the
	// live keystore: reconfigure and cert rotation regenerate it under a running platform, so a
	// keytool failure there would leave the install with no keystore to serve from. Build it
	// alongside and rename, which is atomic within the directory.
	stagedPath := keystorePath + ".new"
	if err := os.RemoveAll(stagedPath); err != nil {
		return fmt.Errorf("remove stale staged keystore %s: %w", stagedPath, err)
	}
	keytoolPath, err := javaBinary("keytool")
	if err != nil {
		return err
	}
	out = shell.RunWithEnvVars(keytoolPath, passEnv, "-importkeystore",
		"-srckeystore", intermediate,
		"-srcstoretype", "PKCS12",
		"-srcstorepass:env", keystorePasswordEnvVar,
		"-destkeystore", stagedPath,
		"-deststoretype", "BCFKS",
		"-deststorepass:env", keystorePasswordEnvVar,
		"-providerclass", "org.bouncycastle.jcajce.provider.BouncyCastleFipsProvider",
		"-providerpath", bcFipsJar,
		"-noprompt")
	if !out.Succeeded() {
		os.Remove(stagedPath)
		return fmt.Errorf("keytool import to BCFKS: %w: %s", out.Error, keystoreToolOutput(out))
	}
	if err := os.Rename(stagedPath, keystorePath); err != nil {
		os.Remove(stagedPath)
		return fmt.Errorf("move the new keystore into %s: %w", keystorePath, err)
	}
	log.Debug("Generated BCFKS keystore at " + keystorePath)

	if HasSudoAccess() {
		username := viper.GetString("service_username")
		if err := Chown(outDir, username, username, true); err != nil {
			return fmt.Errorf("chown keystore dir: %w", err)
		}
	}
	return nil
}

const (
	// PerfAdvisorKeystoreName is the keystore Perf Advisor serves TLS from.
	PerfAdvisorKeystoreName = "tls.bcfks"
	// perfAdvisorLegacyKeystoreName is the PKCS12 keystore releases before BCFKS produced.
	perfAdvisorLegacyKeystoreName = "tls.p12"
	perfAdvisorKeystoreAlias      = "perf-advisor"
)

// GeneratePerfAdvisorTLSKeystore creates the BCFKS keystore (tls.bcfks) Perf Advisor serves TLS
// from, out of the given PEM cert and key. outDir is the directory to write it into (e.g.
// GetPerfAdvisorCertsDir()). password is the keystore password.
//
// Produced whether or not FIPS mode is on, so turning the flag on does not need new certificates.
func GeneratePerfAdvisorTLSKeystore(certPath, keyPath, outDir, password string) error {
	keystorePath := filepath.Join(outDir, PerfAdvisorKeystoreName)
	if err := writeBCFKSKeystore(certPath, keyPath, keystorePath,
		perfAdvisorKeystoreAlias, password); err != nil {
		return err
	}
	// Upgrades from a PKCS12 release leave the old keystore behind; nothing reads it any more.
	legacyPath := filepath.Join(outDir, perfAdvisorLegacyKeystoreName)
	if err := os.Remove(legacyPath); err != nil && !os.IsNotExist(err) {
		log.Warn("Could not remove superseded keystore " + legacyPath + ": " + err.Error())
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
