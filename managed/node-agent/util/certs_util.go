// Copyright (c) YugabyteDB, Inc.

package util

import (
	"context"
	"crypto"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"errors"
	"fmt"
	"node-agent/model"
	"os"
	"path/filepath"
	"time"

	"github.com/golang-jwt/jwt/v5"
)

// Claims for the JWT.
type Claims struct {
	JwtClientIdClaim string `json:"clientId"`
	JwtUserIdClaim   string `json:"userId"`
	jwt.RegisteredClaims
}

// Saves the cert and key to the certs directory.
func SaveCerts(
	ctx context.Context,
	config *Config,
	nodeAgentConfig *model.NodeAgentConfig,
	subDir string,
) error {
	certsDir := filepath.Join(CertsDir(), subDir)
	err := os.MkdirAll(certsDir, os.ModePerm)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while creating current certs dir %s", certsDir)
		return err
	}
	// File name to content mappings to save into files.
	mappings := map[string]string{
		NodeAgentCertFile:    nodeAgentConfig.ServerCert,
		NodeAgentKeyFile:     nodeAgentConfig.ServerKey,
		SignerPublicKeyFile:  nodeAgentConfig.SignerPublicKey,
		SignerPrivateKeyFile: nodeAgentConfig.SignerPrivateKey,
	}
	for filename, content := range mappings {
		fileFilepath := filepath.Join(certsDir, filename)
		err = os.WriteFile(fileFilepath, []byte(content), 0644)
		if err != nil {
			FileLogger().Errorf(ctx, "Error while saving %s to %s", filename, fileFilepath)
			return err
		}
	}
	FileLogger().Infof(ctx, "Saved new certs to %s", certsDir)
	return nil
}

// DeleteCertsExcept deletes all the certs except the given cert directories.
func DeleteCertsExcept(ctx context.Context, certDirs []string) error {
	certDirsMap := map[string]struct{}{}
	for _, certDir := range certDirs {
		certDirsMap[certDir] = struct{}{}
	}
	return ScanDir(CertsDir(), func(fInfo os.FileInfo) (bool, error) {
		name := fInfo.Name()
		delete := false
		if fInfo.IsDir() {
			if certDirs == nil {
				delete = true
			} else if _, ok := certDirsMap[name]; !ok {
				delete = true
			}
		}
		if delete {
			if err := DeleteCerts(ctx, name); err != nil {
				return false, err
			}
		}
		return true, nil
	})
}

// DeleteCerts deletes all the certs in the given cert directory.
func DeleteCerts(ctx context.Context, certDir string) error {
	certsPath := filepath.Join(CertsDir(), certDir)
	FileLogger().Infof(ctx, "Deleting certs %s", certsPath)
	err := os.RemoveAll(certsPath)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while deleting certs %s, err %s", certsPath, err.Error())
	}
	return err
}

// DeleteReleasesExcept deletes all releases except the given release.
func DeleteReleasesExcept(ctx context.Context, release string) error {
	return ScanDir(ReleaseDir(), func(fInfo os.FileInfo) (bool, error) {
		name := fInfo.Name()
		if release != name && fInfo.IsDir() {
			err := DeleteRelease(ctx, name)
			if err != nil {
				return false, err
			}
		}
		return true, nil
	})
}

// DeleteCerts deletes a release.
func DeleteRelease(ctx context.Context, release string) error {
	releaseDir := filepath.Join(ReleaseDir(), release)
	FileLogger().Infof(ctx, "Deleting release dir %s", releaseDir)
	err := os.RemoveAll(releaseDir)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while deleting release dir %s, err %s", release, err.Error())
	}
	return err
}

// ServerCertPath returns the path for the server certificate.
func ServerCertPath(config *Config) string {
	return filepath.Join(
		CertsDir(),
		config.String(PlatformCertsKey),
		NodeAgentCertFile,
	)
}

// ServerCertPaths returns both old and new paths.
func ServerCertPaths(config *Config) []string {
	return CertFilePaths(config, NodeAgentCertFile)
}

// SignerPublicKeyPaths returns the paths for the signer public keys including the old and new paths.
func SignerPublicKeyPaths(config *Config) []string {
	return CertFilePaths(config, SignerPublicKeyFile)
}

// SignerPrivateKeyPaths returns the paths for the signer private keys including the old and new paths.
func SignerPrivateKeyPaths(config *Config) []string {
	return CertFilePaths(config, SignerPrivateKeyFile)
}

// ServerKeyPath returns the path for the server key.
func ServerKeyPath(config *Config) string {
	return filepath.Join(
		CertsDir(),
		config.String(PlatformCertsKey),
		NodeAgentKeyFile,
	)
}

// SignerPrivateKeyPath returns the path for the signer private key.
func SignerPrivateKeyPath(config *Config) string {
	return filepath.Join(
		CertsDir(),
		config.String(PlatformCertsKey),
		SignerPrivateKeyFile,
	)
}

// CertFilePaths returns the paths for the cert related files.
func CertFilePaths(config *Config, filename string) []string {
	paths := []string{}
	keys := []string{PlatformCertsKey, PlatformCertsUpgradeKey}
	for _, key := range keys {
		val := config.String(key)
		if val == "" {
			continue
		}
		path := filepath.Join(CertsDir(), val, filename)
		if _, err := os.Stat(path); os.IsNotExist(err) {
			continue
		}
		paths = append(paths, path)
	}
	return paths
}

// Creates a new JWT with the required claims: Node Id and User Id.
// The JWT is signed using the key in the certs directory.
func GenerateJWT(ctx context.Context, config *Config) (string, error) {
	keyFilepath := SignerPrivateKeyPath(config)
	privateKey, err := os.ReadFile(keyFilepath)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while reading the private key: %s", err.Error())
		return "", err
	}
	key, err := jwt.ParseRSAPrivateKeyFromPEM(privateKey)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while parsing the private key: %s", err.Error())
		return "", err
	}
	claims := &Claims{
		JwtClientIdClaim: config.String(NodeAgentIdKey),
		JwtUserIdClaim:   config.String(UserIdKey),
		RegisteredClaims: jwt.RegisteredClaims{
			IssuedAt:  jwt.NewNumericDate(time.Now()),
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(JwtExpirationSecs * time.Second)),
			Issuer:    JwtIssuer,
			Subject:   JwtSubject,
		},
	}
	FileLogger().Infof(ctx, "Created JWT using %s key", config.String(PlatformCertsKey))
	token := jwt.NewWithClaims(jwt.SigningMethodRS256, claims)
	return token.SignedString(key)
}

// PublicKeyFromFile extracts an RSA public key from a PEM public key file.
func PublicKeyFromFile(ctx context.Context, keyFilepath string) (crypto.PublicKey, error) {
	bytes, err := os.ReadFile(keyFilepath)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while reading the public key: %s", err.Error())
		return nil, err
	}
	key, err := jwt.ParseRSAPublicKeyFromPEM(bytes)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while parsing the public key: %s", err.Error())
		return nil, err
	}
	return key, nil
}

// PublicKeyFromCert extracts public key from a cert.
func PublicKeyFromCert(ctx context.Context, certFilepath string) (crypto.PublicKey, error) {
	bytes, err := os.ReadFile(certFilepath)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while reading the certificate: %s", err.Error())
		return nil, err
	}
	block, _ := pem.Decode(bytes)
	if block == nil {
		err = errors.New("Failed to decode PEM block from certificate")
		FileLogger().Errorf(ctx, "Error - %s", err.Error())
		return nil, err
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while parsing the certificate: %s", err.Error())
		return nil, err
	}
	if cert.PublicKeyAlgorithm != x509.RSA {
		err = errors.New("RSA public key is expected")
		FileLogger().Errorf(ctx, "Error - %s", err.Error())
		return nil, err
	}
	return cert.PublicKey, nil
}

// PublicKey returns the public key.
func PublicKey(ctx context.Context, config *Config) (crypto.PublicKey, error) {
	return PublicKeyFromCert(ctx, ServerCertPath(config))
}

// SignerPublicKeys returns all the signer public keys.
func SignerPublicKeys(ctx context.Context, config *Config) []crypto.PublicKey {
	keys := []crypto.PublicKey{}
	for _, path := range SignerPublicKeyPaths(config) {
		key, err := PublicKeyFromFile(ctx, path)
		if err != nil {
			FileLogger().Errorf(ctx, "Error in getting the signer public key from file: %s", err.Error())
			continue
		}
		keys = append(keys, key)
	}
	return keys
}

// CertPublicKeys returns all the public keys from the cert files including the old and new paths.
func CertPublicKeys(ctx context.Context, config *Config) []crypto.PublicKey {
	keys := []crypto.PublicKey{}
	for _, path := range ServerCertPaths(config) {
		key, err := PublicKeyFromCert(ctx, path)
		if err != nil {
			// Ignore the error for backward compatibility.
			FileLogger().Errorf(ctx, "Error in getting the public key from cert: %s", err.Error())
			continue
		}
		keys = append(keys, key)
	}
	return keys
}

// IntFromClaims returns the int value for key in the map claims.
func IntFromClaims(claims *jwt.MapClaims, key string) (int64, bool) {
	if claims != nil {
		mp := map[string]interface{}(*claims)
		if i, ok := mp[key]; ok {
			switch dType := i.(type) {
			case float64:
				return int64(dType), true
			case json.Number:
				v, _ := dType.Int64()
				return v, true
			default:
				if v, ok := i.(int64); ok {
					return v, true
				}
			}
		}
	}
	return 0, false
}

// StringFromClaims returns the string value for key in the map claims.
func StringFromClaims(claims *jwt.MapClaims, key string) (string, bool) {
	if claims != nil {
		mp := map[string]interface{}(*claims)
		if i, ok := mp[key]; ok {
			if v, ok := i.(string); ok {
				return v, true
			}
		}
	}
	return "", false
}

// ExtractClaims extracts the claims in the auth token.
func ExtractClaims(ctx context.Context, config *Config, authToken string) (*jwt.MapClaims, error) {
	parser := jwt.NewParser()
	token, _, err := parser.ParseUnverified(authToken, &jwt.MapClaims{})
	if err != nil {
		return nil, fmt.Errorf("Invalid token")
	}
	return token.Claims.(*jwt.MapClaims), nil
}

// VerifyJWT verifies the JWT and returns the claims.
func VerifyJWT(ctx context.Context, config *Config, authToken string) (*jwt.MapClaims, error) {
	publicKeys := SignerPublicKeys(ctx, config)
	claims, err := verifyJWTWithKeys(ctx, authToken, publicKeys)
	if err == nil {
		return claims, nil
	}
	// Backward compatibility.
	publicKeys = CertPublicKeys(ctx, config)
	claims, err = verifyJWTWithKeys(ctx, authToken, publicKeys)
	if err == nil {
		return claims, nil
	}
	return nil, fmt.Errorf("Invalid token")
}

func verifyJWTWithKeys(
	ctx context.Context,
	authToken string,
	publicKeys []crypto.PublicKey,
) (*jwt.MapClaims, error) {
	for _, signerPublicKey := range publicKeys {
		token, err := jwt.ParseWithClaims(
			authToken,
			&jwt.MapClaims{},
			func(token *jwt.Token) (interface{}, error) {
				_, ok := token.Method.(*jwt.SigningMethodRSA)
				if !ok {
					return nil, fmt.Errorf("Unexpected token signing method")
				}
				return signerPublicKey, nil
			},
		)
		if err == nil {
			return token.Claims.(*jwt.MapClaims), nil
		}
	}
	return nil, fmt.Errorf("Invalid token")
}

// TlsConfig returns the common TLS config to be used.
func TlsConfig(certs []tls.Certificate, nextProtos []string) *tls.Config {
	whitelists := []uint16{}
	for _, suites := range tls.CipherSuites() {
		// Method excludes insecure ones but the check is just added to be more cautious.
		if !suites.Insecure {
			whitelists = append(whitelists, suites.ID)
		}
	}
	tlsConfig := &tls.Config{
		Certificates: certs,
		MinVersion:   tls.VersionTLS12,
		CipherSuites: whitelists,
	}
	if len(nextProtos) > 0 {
		tlsConfig.NextProtos = nextProtos
	}
	return tlsConfig
}

// CertExpirySecs returns the expiry seconds for the certificate.
func CertExpirySecs(cert *tls.Certificate) (int64, error) {
	leaf, err := x509.ParseCertificate(cert.Certificate[0])
	if err != nil {
		return 0, err
	}
	return leaf.NotAfter.Unix(), nil
}
