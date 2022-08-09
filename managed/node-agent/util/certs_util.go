/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
	"fmt"
	"io/ioutil"
	"os"
	"time"

	"github.com/golang-jwt/jwt/v4"
)

//Claims for the JWT.
type Claims struct {
	JwtClientIdClaim string `json:"clientId"`
	JwtUserIdClaim   string `json:"userId"`
	jwt.StandardClaims
}

//Saves the cert and key to the certs directory.
func SaveCerts(config *Config, cert string, key string, subDir string) error {
	certsDir := GetCertsDir()
	err := os.MkdirAll(fmt.Sprintf("%s/%s", certsDir, subDir), os.ModePerm)
	err = ioutil.WriteFile(
		fmt.Sprintf("%s/%s/%s", certsDir, subDir, AgentCertFile),
		[]byte(cert),
		0644,
	)
	if err != nil {
		FileLogger.Errorf("Error while saving certs to %s/%s/%s", certsDir, subDir, AgentKeyFile)
		return err
	}
	err = ioutil.WriteFile(
		fmt.Sprintf("%s/%s/%s", certsDir, subDir, AgentKeyFile),
		[]byte(key),
		0644,
	)
	if err != nil {
		FileLogger.Errorf("Error while saving key to %s/%s/%s", certsDir, subDir, AgentKeyFile)
		return err
	}
	FileLogger.Infof("Saved new certs to %s/%s", certsDir, subDir)
	return nil
}

func DeleteCerts(subDir string) error {
	FileLogger.Infof("Deleting certs %s", subDir)
	certsDir := GetCertsDir()
	err := os.RemoveAll(fmt.Sprintf("%s/%s", certsDir, subDir))
	if err != nil {
		FileLogger.Errorf("Error while deleting certs %s, err %s", subDir, err.Error())
	}
	return err
}

func DeleteRelease(release string) error {
	FileLogger.Infof("Deleting release dir %s", release)
	releaseDir := GetReleaseDir()
	err := os.RemoveAll(fmt.Sprintf("%s/%s", releaseDir, release))
	if err != nil {
		FileLogger.Errorf("Error while deleting release dir %s, err %s", release, err.Error())
	}
	return err
}

//Creates a new JWT with the required claims:
//Node Id and User Id. The JWT is signed using
//the key in the certs directory.
func GenerateJWT(config *Config) (string, error) {
	certsDir := GetCertsDir()
	privateKey, err := ioutil.ReadFile(
		fmt.Sprintf("%s/%s/%s", certsDir, config.GetString(PlatformCerts), AgentKeyFile),
	)
	if err != nil {
		FileLogger.Errorf("Error while reading the private key: %s", err.Error())
		return "", err
	}
	key, err := jwt.ParseRSAPrivateKeyFromPEM(privateKey)
	if err != nil {
		FileLogger.Errorf("Error while parsing the private key: %s", err.Error())
		return "", err
	}
	claims := &Claims{
		JwtClientIdClaim: config.GetString(NodeAgentId),
		JwtUserIdClaim:   config.GetString(UserId),
		StandardClaims: jwt.StandardClaims{
			IssuedAt:  time.Now().Unix(),
			ExpiresAt: time.Now().Unix() + JwtExpirationTime,
			Issuer:    JwtIssuer,
			Subject:   JwtSubject,
		},
	}
	FileLogger.Infof("Created JWT using %s key", config.GetString(PlatformCerts))
	token := jwt.NewWithClaims(jwt.SigningMethodRS256, claims)
	return token.SignedString(key)
}
