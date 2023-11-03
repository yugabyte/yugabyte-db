/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
	"context"
	"io/ioutil"
	"os"
	"testing"
)

func TestSaveCerts(t *testing.T) {
	certString, keyString := "test-cert", "test-key"
	config := CurrentConfig()
	err := SaveCerts(context.TODO(), config, certString, keyString, "test1")
	if err != nil {
		t.Fatalf("Error while saving certs - %s ", err.Error())
	}

	dir := CertsDir()
	if err != nil {
		t.Fatalf("Error while getting certs dir - %s ", err.Error())
	}
	//Check if the certs are saved and check the value of the files.
	path := dir + "/test1/" + NodeAgentCertFile
	if _, err := os.Stat(path); err == nil {
		privateKey, err := ioutil.ReadFile(path)
		if err != nil {
			t.Fatalf("Unable to read certs - %s", err.Error())
		}
		if string(privateKey) != certString {
			t.Fatalf("Incorrect cert data")
		}
	} else {
		t.Fatalf("Certs not saved properly - %s", err.Error())
	}
}

func TestCreateJWTToken(t *testing.T) {
	config := CurrentConfig()
	_, err := GenerateJWT(context.TODO(), config)
	if err != nil {
		t.Fatalf("Error generating JWT - %s", err.Error())
	}
}

func TestVerifyJWTToken(t *testing.T) {
	config := CurrentConfig()
	jwtToken, err := GenerateJWT(context.TODO(), config)
	if err != nil {
		t.Fatalf("Error generating JWT - %s", err.Error())
	}
	claims, err := VerifyJWT(context.TODO(), config, jwtToken)
	if err != nil {
		t.Fatalf("Error verifying JWT %s", err.Error())
	}
	mapClaims := *claims
	if mapClaims[JwtClientIdClaim] != config.String(NodeAgentIdKey) {
		t.Fatalf(
			"Expected %s, found %s in %v",
			config.String(NodeAgentIdKey),
			mapClaims[JwtClientIdClaim],
			mapClaims,
		)
	}
}
