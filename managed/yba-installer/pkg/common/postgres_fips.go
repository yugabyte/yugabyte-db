// Copyright (c) YugabyteDB, Inc.

package common

import (
	"fmt"
	"os"
	"strings"
)

// PostgresOpenSSLFipsConfig is the OpenSSL config the bundled postgres runs with in FIPS mode. It
// includes fipsModuleCnf, which `openssl fipsinstall` writes on the host, and makes the FIPS
// provider the only source of algorithms; base is only for encoders and decoders.
func PostgresOpenSSLFipsConfig(fipsModuleCnf string) string {
	return fmt.Sprintf(`config_diagnostics = 1
openssl_conf = openssl_init

.include %s

[openssl_init]
providers = provider_sect
alg_section = algorithm_sect

[provider_sect]
fips = fips_sect
base = base_sect

[base_sect]
activate = 1

[algorithm_sect]
default_properties = fips=yes
`, fipsModuleCnf)
}

// PgQuoteIdent quotes a postgres identifier, like quote_ident().
func PgQuoteIdent(name string) string {
	return `"` + strings.ReplaceAll(name, `"`, `""`) + `"`
}

// PgQuoteLiteral quotes a postgres string literal, like quote_literal(). As an escape string
// constant it reads the same whatever standard_conforming_strings is set to.
func PgQuoteLiteral(value string) string {
	escaped := strings.ReplaceAll(value, `\`, `\\`)
	escaped = strings.ReplaceAll(escaped, `'`, `''`)
	return `E'` + escaped + `'`
}

// systemCABundles are the CA bundle locations of the distros yba-ctl supports.
var systemCABundles = []string{
	"/etc/pki/tls/certs/ca-bundle.crt",   // RHEL, Alma, Rocky, Oracle, Amazon Linux
	"/etc/ssl/certs/ca-certificates.crt", // Debian, Ubuntu
	"/etc/ssl/ca-bundle.pem",             // SUSE
}

// SystemCABundle returns the host's CA bundle, or "" if none is found. The bundled postgres's
// OpenSSL has no CA store of its own on the host, so LDAP auth over TLS needs it as SSL_CERT_FILE.
func SystemCABundle() string {
	for _, path := range systemCABundles {
		if _, err := os.Stat(path); err == nil {
			return path
		}
	}
	return ""
}
