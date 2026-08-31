/*
 * Copyright (c) YugabyteDB, Inc.
 */

package checks

import (
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"net"
	"os"
	"strings"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// ServerCertHostname validates that a user provided https server cert is valid for the host YBA is
// served on. Runs on install and reconfigure, the commands that can set a new server_cert_path.
// Self signed certs are generated with the configured host as their subject alternative name, so
// they match by construction and are not checked.
var ServerCertHostname = &serverCertHostnameCheck{"server-cert-hostname", true}

type serverCertHostnameCheck struct {
	name        string
	skipAllowed bool
}

// Name gets the name of the check
func (s serverCertHostnameCheck) Name() string {
	return s.name
}

// SkipAllowed gets if the check can be skipped
func (s serverCertHostnameCheck) SkipAllowed() bool {
	return s.skipAllowed
}

// Execute runs the server cert hostname check. A cert that does not cover the host YBA is served
// on is rejected by browsers and any other client that verifies hostnames, so catch the mismatch
// before the install instead of after.
func (s serverCertHostnameCheck) Execute() Result {
	res := Result{
		Check:  s.name,
		Status: StatusPassed,
	}

	certPath := viper.GetString("server_cert_path")
	if certPath == "" {
		log.Debug("no custom server cert given, self signed certs always match the host")
		return res
	}

	hosts, err := hostsToVerify()
	if err != nil {
		res.Status = StatusCritical
		res.Error = err
		return res
	}

	res.Status, res.Error = checkCertHostnames(certPath, hosts)
	return res
}

// checkCertHostnames verifies that the cert at certPath is valid for every host given. Passes if
// the cert has no SAN or CN at all, or if every host is covered by one of them. A cert that covers
// some but not all of the hosts only warns, and is critical when it covers none of them.
func checkCertHostnames(certPath string, hosts []string) (Status, error) {
	if len(hosts) == 0 {
		return StatusPassed, nil
	}
	cert, err := parseLeafCertFromPem(certPath)
	if err != nil {
		return StatusCritical, err
	}

	if len(cert.DNSNames) == 0 && len(cert.IPAddresses) == 0 && cert.Subject.CommonName == "" {
		log.Warn("Cert " + certPath + " has no SAN or CN")
		return StatusPassed, nil
	}

	// First, verify hosts via SAN. Narrow down the hosts still to account for as we go, but keep the
	// full list to tell a cert that covers some of them from one that covers none.
	unmatched := hosts
	if len(cert.DNSNames) > 0 || len(cert.IPAddresses) > 0 {
		unmatched = unmatchedHosts(cert, unmatched)
		if len(unmatched) == 0 {
			return StatusPassed, nil
		}
		log.DebugLF("server cert " + certPath + " does not cover hosts " +
			strings.Join(unmatched, ", ") + " with SAN, checking Common Name")
	}

	// Hostname verification dropped the legacy Common Name fallback, but yba-ctl.yml.reference
	// documents a matching SAN or CN as valid. Accept a host that only the CN covers rather than
	// fail the install over it - that is what we told users to provide, even though modern TLS
	// clients no longer accept it. The CN is tried for whatever the SAN left unmatched, not only
	// for certs with no SAN at all, since a cert can cover some hosts by SAN and the rest by CN.
	if cert.Subject.CommonName != "" {
		unmatched = unmatchedHosts(commonNameAsSAN(cert), unmatched)
		if len(unmatched) == 0 {
			return StatusPassed, nil
		}
	}

	sanNames := make([]string, 0, len(cert.DNSNames)+len(cert.IPAddresses))
	sanNames = append(sanNames, cert.DNSNames...)
	for _, ip := range cert.IPAddresses {
		sanNames = append(sanNames, ip.String())
	}
	certErr := fmt.Errorf(
		"server cert %s does not cover host %s, the cert has SAN %s and CN '%s'.",
		certPath, strings.Join(unmatched, ", "), sanNames, cert.Subject.CommonName)

	// YBA can be served on several hosts, and a cert that covers only some of them still serves
	// those fine - only clients reaching YBA on the hosts it misses reject it. Warn rather than
	// block the install in that case, and fail only when the cert covers none of the hosts.
	if len(unmatched) < len(hosts) {
		return StatusWarning, certErr
	}
	return StatusCritical, certErr
}

// hostsToVerify gets the hostnames YBA will be served on. On install, 'host' is not defaulted until
// common.Install runs FixConfigValues, which happens after preflight, so mirror that defaulting
// here to validate the cert against the host we will actually use.
func hostsToVerify() ([]string, error) {
	if hosts := common.SplitInput(viper.GetString("host")); len(hosts) > 0 {
		return hosts, nil
	}
	host, err := guessPrimaryIP()
	if err != nil {
		return nil, fmt.Errorf("'host' is not set in %s, and the primary IP to validate the "+
			"server cert against could not be determined: %w", common.InputFile(), err)
	}
	return []string{host}, nil
}

// guessPrimaryIP wraps common.GuessPrimaryIP, which panics when it cannot open a socket.
func guessPrimaryIP() (host string, err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
	}()
	return common.GuessPrimaryIP(), nil
}

// unmatchedHosts returns the subset of hosts the cert is not valid for.
func unmatchedHosts(cert *x509.Certificate, hosts []string) []string {
	unmatched := make([]string, 0)
	for _, host := range hosts {
		if err := cert.VerifyHostname(host); err != nil {
			log.DebugLF("server cert is not valid for host " + host + ": " + err.Error())
			unmatched = append(unmatched, host)
		}
	}
	return unmatched
}

// commonNameAsSAN copies the cert with its Common Name promoted to a subject alternative name, so
// the CN is matched with the same wildcard and IP handling as a SAN. The copy replaces only the SAN
// entries of the CN's own kind - a DNS CN overwrites DNSNames, an IP CN overwrites IPAddresses - so
// entries of the other kind survive. Those cannot widen what this pass accepts, as any host they
// match was already matched while checking the cert's own SAN.
func commonNameAsSAN(cert *x509.Certificate) *x509.Certificate {
	clone := *cert
	if ip := net.ParseIP(cert.Subject.CommonName); ip != nil {
		clone.IPAddresses = []net.IP{ip}
	} else {
		clone.DNSNames = []string{cert.Subject.CommonName}
	}
	return &clone
}

// parseLeafCertFromPem reads the first cert out of a pem file. Custom certs are often given as a
// full chain, so any additional blocks are ignored - the leaf is the cert clients verify the
// hostname against.
func parseLeafCertFromPem(certPath string) (*x509.Certificate, error) {
	certData, err := os.ReadFile(certPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read server cert: %w", err)
	}
	for len(certData) > 0 {
		var block *pem.Block
		block, certData = pem.Decode(certData)
		if block == nil {
			break
		}
		// A pem file may hold the private key next to the certs, skip anything that is not a cert.
		if block.Type != "CERTIFICATE" && block.Type != "X509 CERTIFICATE" &&
			block.Type != "TRUSTED CERTIFICATE" {
			continue
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			return nil, fmt.Errorf("failed to parse server cert %s: %w", certPath, err)
		}
		return cert, nil
	}
	return nil, fmt.Errorf("no certificate found in server cert %s, expected a pem encoded cert",
		certPath)
}
