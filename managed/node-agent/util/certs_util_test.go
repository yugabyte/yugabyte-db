/*
 * Copyright (c) YugabyteDB, Inc.
 */
package util

import (
	"context"
	"crypto/rsa"
	"node-agent/model"
	"os"
	"path/filepath"
	"testing"
)

func TestSaveCerts(t *testing.T) {
	certString, keyString := "test-cert", "test-key"
	signerPrivate, signerPublic := GetSignerPublicAndPrivateKey()
	config := CurrentConfig()
	nodeAgentConfig := &model.NodeAgentConfig{
		ServerCert:       certString,
		ServerKey:        keyString,
		SignerPublicKey:  string(signerPublic),
		SignerPrivateKey: string(signerPrivate),
	}
	subDir := "test1"
	err := SaveCerts(context.TODO(), config, nodeAgentConfig, subDir)
	if err != nil {
		t.Fatalf("Error while saving certs - %s ", err.Error())
	}

	dir := filepath.Join(CertsDir(), subDir)
	checks := map[string]string{
		NodeAgentCertFile:    certString,
		NodeAgentKeyFile:     keyString,
		SignerPublicKeyFile:  string(signerPublic),
		SignerPrivateKeyFile: string(signerPrivate),
	}
	for filename, expected := range checks {
		path := filepath.Join(dir, filename)
		data, err := os.ReadFile(path)
		if err != nil {
			t.Fatalf("Unable to read %s - %s", filename, err.Error())
		}
		if string(data) != expected {
			t.Fatalf("Incorrect data in %s", filename)
		}
	}
}

func TestPublicKeyFromFile(t *testing.T) {
	config := CurrentConfig()
	paths := SignerPublicKeyPaths(config)
	if len(paths) == 0 {
		t.Fatal("Expected at least one signer public key path from test setup")
	}
	key, err := PublicKeyFromFile(context.TODO(), paths[0])
	if err != nil {
		t.Fatalf("Error loading signer public key - %s", err.Error())
	}
	if _, ok := key.(*rsa.PublicKey); !ok {
		t.Fatalf("Expected *rsa.PublicKey, got %T", key)
	}
}

func TestSignerPublicKeys(t *testing.T) {
	config := CurrentConfig()
	keys := SignerPublicKeys(context.TODO(), config)
	if len(keys) == 0 {
		t.Fatal("Expected signer public keys from test setup")
	}
	signerPaths := SignerPublicKeyPaths(config)
	if len(signerPaths) == 0 {
		t.Fatal("Expected signer public key path from test setup")
	}
	signerKey, err := PublicKeyFromFile(context.TODO(), signerPaths[0])
	if err != nil {
		t.Fatalf("Error loading signer public key - %s", err.Error())
	}
	signerRSA := signerKey.(*rsa.PublicKey)
	found := false
	for _, key := range keys {
		if pub, ok := key.(*rsa.PublicKey); ok && pub.Equal(signerRSA) {
			found = true
			break
		}
	}
	if !found {
		t.Fatal("Signer public key was not included in SignerPublicKeys result")
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

func TestVerifyJWTTokenWithSignerKeyOnly(t *testing.T) {
	config := CurrentConfig()
	// Remove server cert so verification must use signer.pub.
	certPath := ServerCertPath(config)
	certBackup, err := os.ReadFile(certPath)
	if err != nil {
		t.Fatalf("Unable to read server cert - %s", err.Error())
	}
	if err := os.Remove(certPath); err != nil {
		t.Fatalf("Unable to remove server cert - %s", err.Error())
	}
	defer os.WriteFile(certPath, certBackup, 0644)

	jwtToken, err := GenerateJWT(context.TODO(), config)
	if err != nil {
		t.Fatalf("Error generating JWT - %s", err.Error())
	}
	claims, err := VerifyJWT(context.TODO(), config, jwtToken)
	if err != nil {
		t.Fatalf("Error verifying JWT with signer key only - %s", err.Error())
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
