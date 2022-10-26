/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
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
	certsDir := filepath.Join(CertsDir(), subDir)
	FileLogger().Infof("Deleting certs %s", certsDir)
	err := os.RemoveAll(certsDir)
	if err != nil {
		FileLogger().Errorf("Error while deleting certs %s, err %s", certsDir, err.Error())
	}
	return err
}

func DeleteRelease(release string) error {
	releaseDir := filepath.Join(ReleaseDir(), release)
	FileLogger().Infof("Deleting release dir %s", releaseDir)
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

func PublicKey(config *Config) (crypto.PublicKey, error) {
	certFilepath := ServerCertPath(config)
	bytes, err := ioutil.ReadFile(certFilepath)
	if err != nil {
		FileLogger().Errorf("Error while reading the certificate: %s", err.Error())
		return nil, err
	}
	block, _ := pem.Decode(bytes)
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		FileLogger().Errorf("Error while parsing the certificate: %s", err.Error())
		return nil, err
	}
	if cert.PublicKeyAlgorithm != x509.RSA {
		err = errors.New("RSA public key is expected")
		FileLogger().Errorf("Error - %s", err.Error())
		return nil, err
	}
	return cert.PublicKey, nil
}

func VerifyJWT(config *Config, authToken string) (*jwt.MapClaims, error) {
	publicKey, err := PublicKey(config)
	if err != nil {
		FileLogger().Errorf("Error in getting the public key: %s", err.Error())
		return nil, err
	}
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
	if err != nil {
		return nil, fmt.Errorf("Invalid token: %w", err)
	}
	return token.Claims.(*jwt.MapClaims), nil
}
