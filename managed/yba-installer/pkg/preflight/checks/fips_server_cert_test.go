package checks

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/spf13/viper"
)

type testCA struct {
	cert *x509.Certificate
	key  crypto.Signer
}

func fipsTestKey(t *testing.T, kind string) crypto.Signer {
	t.Helper()
	var key crypto.Signer
	var err error
	switch kind {
	case "rsa1024":
		key, err = rsa.GenerateKey(rand.Reader, 1024)
	case "rsa2048":
		key, err = rsa.GenerateKey(rand.Reader, 2048)
	case "rsa3000":
		key, err = rsa.GenerateKey(rand.Reader, 3000)
	case "p224":
		key, err = ecdsa.GenerateKey(elliptic.P224(), rand.Reader)
	case "p256":
		key, err = ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	case "p384":
		key, err = ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	case "p521":
		key, err = ecdsa.GenerateKey(elliptic.P521(), rand.Reader)
	case "ed25519":
		_, key, err = ed25519.GenerateKey(rand.Reader)
	default:
		t.Fatalf("unknown key kind %s", kind)
	}
	if err != nil {
		t.Fatal(err)
	}
	return key
}

// fipsTestCert issues a cert for key, signed by issuer, or self-signed when issuer is nil.
func fipsTestCert(
	t *testing.T, cn string, key crypto.Signer, issuer *testCA, sigAlg x509.SignatureAlgorithm,
) *testCA {
	t.Helper()
	template := &x509.Certificate{
		SerialNumber:          big.NewInt(time.Now().UnixNano()),
		Subject:               pkix.Name{CommonName: cn},
		NotBefore:             time.Now(),
		NotAfter:              time.Now().Add(time.Hour),
		SignatureAlgorithm:    sigAlg,
		IsCA:                  issuer == nil,
		BasicConstraintsValid: true,
	}
	parent, signer := template, key
	if issuer != nil {
		parent, signer = issuer.cert, issuer.key
	}
	der, err := x509.CreateCertificate(rand.Reader, template, parent, key.Public(), signer)
	if err != nil {
		t.Fatalf("failed to create %s: %v", cn, err)
	}
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		t.Fatal(err)
	}
	return &testCA{cert, key}
}

func writeFipsTestCerts(t *testing.T, certs ...*testCA) string {
	t.Helper()
	var data []byte
	for _, c := range certs {
		data = append(data, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: c.cert.Raw})...)
	}
	path := filepath.Join(t.TempDir(), "server.pem")
	if err := os.WriteFile(path, data, 0600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestCheckFipsServerCerts(t *testing.T) {
	selfSigned := func(kind string) string {
		return writeFipsTestCerts(t, fipsTestCert(t, "leaf", fipsTestKey(t, kind), nil, 0))
	}
	rsaRoot := fipsTestCert(t, "root", fipsTestKey(t, "rsa2048"), nil, 0)
	sha1Root := fipsTestCert(t, "root", fipsTestKey(t, "rsa2048"), nil, x509.SHA1WithRSA)
	weakRoot := fipsTestCert(t, "root", fipsTestKey(t, "rsa1024"), nil, 0)
	sha1Intermediate := fipsTestCert(t, "intermediate", fipsTestKey(t, "rsa2048"), rsaRoot,
		x509.SHA1WithRSA)

	tests := []struct {
		name string
		path string
		// substring of the error, empty when the certs must be accepted
		wantErr string
	}{
		{"rsa 2048", selfSigned("rsa2048"), ""},
		{"ecdsa p-256", selfSigned("p256"), ""},
		{"ecdsa p-384", selfSigned("p384"), ""},
		{"rsa 1024", selfSigned("rsa1024"), "1024-bit RSA key"},
		{"rsa 3000", selfSigned("rsa3000"), "3000-bit RSA key"},
		{"ecdsa p-224", selfSigned("p224"), "ECDSA P-224 key"},
		{"ecdsa p-521", selfSigned("p521"), "ECDSA P-521 key"},
		{"ed25519", selfSigned("ed25519"), "key type Ed25519"},
		{"sha-1 signed leaf", writeFipsTestCerts(t,
			fipsTestCert(t, "leaf", fipsTestKey(t, "rsa2048"), nil, x509.SHA1WithRSA)),
			"signed with SHA1-RSA"},
		{"chain with a sha-1 self-signed root", writeFipsTestCerts(t,
			fipsTestCert(t, "leaf", fipsTestKey(t, "p256"), sha1Root, 0), sha1Root), ""},
		{"chain with a sha-1 intermediate", writeFipsTestCerts(t,
			fipsTestCert(t, "leaf", fipsTestKey(t, "p256"), sha1Intermediate, 0), sha1Intermediate,
			rsaRoot), "\"CN=intermediate\""},
		{"chain with a 1024-bit root", writeFipsTestCerts(t,
			fipsTestCert(t, "leaf", fipsTestKey(t, "rsa2048"), weakRoot, 0), weakRoot),
			"\"CN=root\""},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := checkFipsServerCerts(tc.path)
			if tc.wantErr == "" && err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tc.wantErr != "" && (err == nil || !strings.Contains(err.Error(), tc.wantErr)) {
				t.Fatalf("error %v, want one containing %q", err, tc.wantErr)
			}
		})
	}
}

func TestFipsServerCertOnlyChecksFipsInstalls(t *testing.T) {
	t.Cleanup(viper.Reset)
	weak := writeFipsTestCerts(t, fipsTestCert(t, "leaf", fipsTestKey(t, "rsa1024"), nil, 0))
	for _, tc := range []struct {
		fips bool
		cert string
		want Status
	}{
		{true, weak, StatusCritical},
		{false, weak, StatusPassed},
		{true, "", StatusPassed},
	} {
		viper.Set("fips.enabled", tc.fips)
		viper.Set("server_cert_path", tc.cert)
		if res := FipsServerCert.Execute(); res.Status != tc.want {
			t.Errorf("fips=%v cert=%q: Execute() = %v, want %v", tc.fips, tc.cert, res, tc.want)
		}
	}
	if FipsServerCert.SkipAllowed() {
		t.Fatal("fips-server-cert must not be skippable")
	}
}
