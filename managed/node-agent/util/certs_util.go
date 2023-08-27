// Copyright (c) YugaByte, Inc.

package util

import (
	"context"
	"crypto"
	"crypto/x509"
	"encoding/pem"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"time"

	"github.com/golang-jwt/jwt/v4"
)

// Claims for the JWT.
type Claims struct {
	JwtClientIdClaim string `json:"clientId"`
	JwtUserIdClaim   string `json:"userId"`
	jwt.StandardClaims
}

// Saves the cert and key to the certs directory.
func SaveCerts(ctx context.Context, config *Config, cert string, key string, subDir string) error {
	certsDir := filepath.Join(CertsDir(), subDir)
	err := os.MkdirAll(certsDir, os.ModePerm)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while creating current certs dir %s", certsDir)
		return err
	}
	certFilepath := filepath.Join(certsDir, NodeAgentCertFile)
	err = ioutil.WriteFile(
		certFilepath,
		[]byte(cert),
		0644,
	)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while saving certs to %s", certFilepath)
		return err
	}
	keyFilepath := filepath.Join(certsDir, NodeAgentKeyFile)
	err = ioutil.WriteFile(
		keyFilepath,
		[]byte(key),
		0644,
	)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while saving key to %s", keyFilepath)
		return err
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

func ServerCertPath(config *Config) string {
	return filepath.Join(
		CertsDir(),
		config.String(PlatformCertsKey),
		NodeAgentCertFile,
	)
}

// ServerCertPaths returns both old and new paths.
func ServerCertPaths(config *Config) []string {
	certPaths := []string{}
	keys := []string{PlatformCertsKey, PlatformCertsUpgradeKey}
	for _, key := range keys {
		val := config.String(key)
		if val == "" {
			continue
		}
		path := filepath.Join(CertsDir(), val, NodeAgentCertFile)
		if _, err := os.Stat(path); os.IsNotExist(err) {
			continue
		}
		certPaths = append(certPaths, path)
	}
	return certPaths
}

func ServerKeyPath(config *Config) string {
	return filepath.Join(
		CertsDir(),
		config.String(PlatformCertsKey),
		NodeAgentKeyFile,
	)
}

// Creates a new JWT with the required claims: Node Id and User Id.
// The JWT is signed using the key in the certs directory.
func GenerateJWT(ctx context.Context, config *Config) (string, error) {
	keyFilepath := ServerKeyPath(config)
	privateKey, err := ioutil.ReadFile(keyFilepath)
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
		StandardClaims: jwt.StandardClaims{
			IssuedAt:  time.Now().Unix(),
			ExpiresAt: time.Now().Unix() + JwtExpirationTime,
			Issuer:    JwtIssuer,
			Subject:   JwtSubject,
		},
	}
	FileLogger().Infof(ctx, "Created JWT using %s key", config.String(PlatformCertsKey))
	token := jwt.NewWithClaims(jwt.SigningMethodRS256, claims)
	return token.SignedString(key)
}

// PublicKeyFromCert extracts public key from a cert.
func PublicKeyFromCert(ctx context.Context, certFilepath string) (crypto.PublicKey, error) {
	bytes, err := ioutil.ReadFile(certFilepath)
	if err != nil {
		FileLogger().Errorf(ctx, "Error while reading the certificate: %s", err.Error())
		return nil, err
	}
	block, _ := pem.Decode(bytes)
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

// PublicKey returns all the public keys including the new one.
func PublicKeys(ctx context.Context, config *Config) ([]crypto.PublicKey, error) {
	keys := []crypto.PublicKey{}
	paths := ServerCertPaths(config)
	for _, path := range paths {
		key, err := PublicKeyFromCert(ctx, path)
		if err != nil {
			return keys, err
		}
		keys = append(keys, key)
	}
	return keys, nil
}

// VerifyJWT verifies the JWT and returns the claims.
func VerifyJWT(ctx context.Context, config *Config, authToken string) (*jwt.MapClaims, error) {
	publicKeys, err := PublicKeys(ctx, config)
	if err != nil {
		FileLogger().Errorf(ctx, "Error in getting the public key: %s", err.Error())
		return nil, err
	}
	for idx, publicKey := range publicKeys {
		token, err := jwt.ParseWithClaims(
			authToken,
			&jwt.MapClaims{},
			func(token *jwt.Token) (interface{}, error) {
				_, ok := token.Method.(*jwt.SigningMethodRSA)
				if !ok {
					return nil, fmt.Errorf("Unexpected token signing method")
				}
				return publicKey, nil
			},
		)
		if err == nil {
			return token.Claims.(*jwt.MapClaims), nil
		}
		if idx == len(publicKeys)-1 {
			// All keys are exhausted.
			FileLogger().Errorf(ctx, "Failed to validate claim - %s", err.Error())
		}
	}
	return nil, fmt.Errorf("Invalid token")
}
