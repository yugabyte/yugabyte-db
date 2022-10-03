/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"io/ioutil"
	"os"
	"testing"
)

func TestSaveCerts(t *testing.T) {
	certString, keyString := "test-cert", "test-key"
	config := CurrentConfig()
	err := SaveCerts(config, certString, keyString, "test1")
	if err != nil {
		t.Errorf("Error while saving certs - %s ", err.Error())
	}

	dir := CertsDir()
	if err != nil {
		t.Errorf("Error while getting certs dir - %s ", err.Error())
	}
	//Check if the certs are saved and check the value of the files.
	path := dir + "/test1/" + NodeAgentCertFile
	if _, err := os.Stat(path); err == nil {
		privateKey, err := ioutil.ReadFile(path)
		if err != nil {
			t.Errorf("Unable to read certs - %s", err.Error())
		}
		if string(privateKey) != certString {
			t.Errorf("Incorrect cert data")
		}
	} else {
		t.Errorf("Certs not saved properly - %s", err.Error())
	}
}

func TestCreateJWTToken(t *testing.T) {
	config := CurrentConfig()
	private, public := getPublicAndPrivateKey()
	err := SaveCerts(config, string(public), string(private), "test2")
	_, err = GenerateJWT(config)
	if err != nil {
		t.Errorf("Error generating JWT")
	}
}

func getPublicAndPrivateKey() ([]byte, []byte) {
	// Generate RSA key.
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		panic(err)
	}
	pub := key.Public()

	privateKey, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		panic(err)
	}

	publicKey, err := x509.MarshalPKIXPublicKey(pub)
	if err != nil {
		panic(err)
	}

	// Encode private key to PEM.
	keyPEM := pem.EncodeToMemory(
		&pem.Block{
			Type:  "RSA PRIVATE KEY",
			Bytes: privateKey,
		},
	)

	// Encode public key to PEM
	pubPEM := pem.EncodeToMemory(
		&pem.Block{
			Type:  "CERTIFICATE",
			Bytes: publicKey,
		},
	)
	return keyPEM, pubPEM
}
