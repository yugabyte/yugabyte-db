/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
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
func SaveCerts(config *Config, cert string, key string, subDir string) error {
	certsDir := filepath.Join(CertsDir(), subDir)
	err := os.MkdirAll(certsDir, os.ModePerm)
	if err != nil {
		FileLogger().Errorf("Error while creating current certs dir %s", certsDir)
		return err
	}
	certFilepath := filepath.Join(certsDir, NodeAgentCertFile)
	err = ioutil.WriteFile(
		certFilepath,
		[]byte(cert),
		0644,
	)
	if err != nil {
		FileLogger().Errorf("Error while saving certs to %s", certFilepath)
		return err
	}
	keyFilepath := filepath.Join(certsDir, NodeAgentKeyFile)
	err = ioutil.WriteFile(
		keyFilepath,
		[]byte(key),
		0644,
	)
	if err != nil {
		FileLogger().Errorf("Error while saving key to %s", keyFilepath)
		return err
	}
	FileLogger().Infof("Saved new certs to %s", certsDir)
	return nil
}

func DeleteCerts(subDir string) error {
	FileLogger().Infof("Deleting certs %s", subDir)
	certsDir := filepath.Join(CertsDir(), subDir)
	err := os.RemoveAll(certsDir)
	if err != nil {
		FileLogger().Errorf("Error while deleting certs %s, err %s", certsDir, err.Error())
	}
	return err
}

func DeleteRelease(release string) error {
	FileLogger().Infof("Deleting release dir %s", release)
	releaseDir := filepath.Join(ReleaseDir(), release)
	err := os.RemoveAll(releaseDir)
	if err != nil {
		FileLogger().Errorf("Error while deleting release dir %s, err %s", release, err.Error())
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

func ServerKeyPath(config *Config) string {
	return filepath.Join(
		CertsDir(),
		config.String(PlatformCertsKey),
		NodeAgentKeyFile,
	)
}

// Creates a new JWT with the required claims: Node Id and User Id.
// The JWT is signed using the key in the certs directory.
func GenerateJWT(config *Config) (string, error) {
	keyFilepath := ServerKeyPath(config)
	privateKey, err := ioutil.ReadFile(keyFilepath)
	if err != nil {
		FileLogger().Errorf("Error while reading the private key: %s", err.Error())
		return "", err
	}
	key, err := jwt.ParseRSAPrivateKeyFromPEM(privateKey)
	if err != nil {
		FileLogger().Errorf("Error while parsing the private key: %s", err.Error())
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
	FileLogger().Infof("Created JWT using %s key", config.String(PlatformCertsKey))
	token := jwt.NewWithClaims(jwt.SigningMethodRS256, claims)
	return token.SignedString(key)
}
