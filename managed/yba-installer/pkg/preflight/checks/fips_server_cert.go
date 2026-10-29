/*
 * Copyright (c) YugabyteDB, Inc.
 */

package checks

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"os"

	"github.com/spf13/viper"
)

// FipsServerCert rejects a custom https server cert that YBA components cannot use in FIPS mode.
// YBA, Perf Advisor and node_exporter would start with it but fail every TLS handshake, or fail
// the ones with yba-ctl, node agent and Prometheus. It cannot be skipped for that reason.
var FipsServerCert = &fipsServerCertCheck{"fips-server-cert", false}

type fipsServerCertCheck struct {
	name        string
	skipAllowed bool
}

func (f fipsServerCertCheck) Name() string {
	return f.name
}

func (f fipsServerCertCheck) SkipAllowed() bool {
	return f.skipAllowed
}

func (f fipsServerCertCheck) Execute() Result {
	res := Result{Check: f.name, Status: StatusPassed}
	certPath := viper.GetString("server_cert_path")
	if !viper.GetBool("fips.enabled") || certPath == "" {
		return res
	}
	if err := checkFipsServerCerts(certPath); err != nil {
		res.Status = StatusCritical
		res.Error = err
	}
	return res
}

// checkFipsServerCerts applies what both FIPS TLS stacks in use accept, which is narrower than
// FIPS 140-3 itself: Go's crypto/tls in FIPS mode takes only RSA 2048/3072/4096 or ECDSA
// P-256/P-384/P-521 certs and cannot sign with P-521, and BouncyCastle JSSE refuses other curves,
// EdDSA and DSA. Both refuse certs signed with SHA-1, except a self-signed root, whose signature
// no client checks.
func checkFipsServerCerts(certPath string) error {
	data, err := os.ReadFile(certPath)
	if err != nil {
		return fmt.Errorf("failed to read server cert: %w", err)
	}
	for i := 0; len(data) > 0; {
		var block *pem.Block
		block, data = pem.Decode(data)
		if block == nil {
			break
		}
		if block.Type != "CERTIFICATE" {
			continue
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			return fmt.Errorf("failed to parse server cert %s: %w", certPath, err)
		}
		if reason := fipsUnsupportedReason(cert, i == 0); reason != "" {
			return fmt.Errorf("certificate %q in %s %s, which YBA does not support in FIPS mode. "+
				"Use RSA 2048, 3072 or 4096-bit or ECDSA P-256 or P-384 keys, signed with SHA-256 "+
				"or stronger", cert.Subject.String(), certPath, reason)
		}
		i++
	}
	return nil
}

func fipsUnsupportedReason(cert *x509.Certificate, leaf bool) string {
	switch key := cert.PublicKey.(type) {
	case *rsa.PublicKey:
		if bits := key.N.BitLen(); bits != 2048 && bits != 3072 && bits != 4096 {
			return fmt.Sprintf("has a %d-bit RSA key", bits)
		}
	case *ecdsa.PublicKey:
		if key.Curve != elliptic.P256() && key.Curve != elliptic.P384() {
			return fmt.Sprintf("has an ECDSA %s key", key.Curve.Params().Name)
		}
	default:
		if cert.PublicKeyAlgorithm == x509.UnknownPublicKeyAlgorithm {
			return "has an unsupported key type"
		}
		return fmt.Sprintf("has key type %s", cert.PublicKeyAlgorithm)
	}
	selfSignedRoot := !leaf && bytes.Equal(cert.RawSubject, cert.RawIssuer)
	switch cert.SignatureAlgorithm {
	case x509.SHA1WithRSA, x509.ECDSAWithSHA1, x509.DSAWithSHA1, x509.MD5WithRSA, x509.MD2WithRSA:
		if !selfSignedRoot {
			return fmt.Sprintf("is signed with %s", cert.SignatureAlgorithm)
		}
	}
	return ""
}
