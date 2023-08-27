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
	"strings"
	"time"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

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
		10*365*24*time.Hour /*10 years*/, "", nil, nil)

	// generate a server cert and key signed by the above root CA
	generateCert(certPath, keyPath, false, /*isCA*/
		4*365*24*time.Hour /*4 years*/, host, caCert, caKey)

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
		resultCert.Subject.Organization = []string{"Yugabyte Self-Signed CA"}
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
