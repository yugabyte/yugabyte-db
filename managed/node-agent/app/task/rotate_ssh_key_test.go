// Copyright (c) YugabyteDB, Inc.

package task

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	pb "node-agent/generated/service"
	"os"
	"path/filepath"
	"testing"

	"golang.org/x/crypto/ssh"
)

// This struct represents an SSH key pair.
type sshKeyPair struct {
	privateKey []byte
	publicKey  []byte
}

// This function generates a given number of SSH key pairs with the given bit size.
func generateSSHKeyPairs(t *testing.T, bits, count int) ([]sshKeyPair, error) {
	sshKeyPairs := []sshKeyPair{}
	for i := 0; i < count; i++ {
		// Generate RSA private key
		privateKey, err := rsa.GenerateKey(rand.Reader, bits)
		if err != nil {
			t.Fatalf("Failed to generate private key: %v", err)
		}
		// Encode private key to PEM format
		privateKeyPEM := pem.EncodeToMemory(&pem.Block{
			Type:  "RSA PRIVATE KEY",
			Bytes: x509.MarshalPKCS1PrivateKey(privateKey),
		})
		// Generate public key in OpenSSH format
		pub, err := ssh.NewPublicKey(&privateKey.PublicKey)
		if err != nil {
			t.Fatalf("Failed to generate public key: %v", err)
		}

		pubBytes := ssh.MarshalAuthorizedKey(pub)
		sshKeyPairs = append(sshKeyPairs, sshKeyPair{
			privateKey: privateKeyPEM,
			publicKey:  pubBytes,
		})
	}
	return sshKeyPairs, nil
}

// This function creates an authorized keys file with the given SSH key pairs.
func createAuthorizedKeysFile(t *testing.T, sshKeyPairs []sshKeyPair) string {
	authorizedKeysPath := filepath.Join(t.TempDir(), "authorized_keys")
	file, err := os.Create(authorizedKeysPath)
	if err != nil {
		t.Fatalf("Failed to create authorized keys file: %v", err)
	}
	defer file.Close()
	for _, sshKeyPair := range sshKeyPairs {
		_, err = file.Write(sshKeyPair.publicKey)
		if err != nil {
			t.Fatalf("Failed to write public key: %v", err)
		}
	}
	t.Logf("Authorized keys file created at %s", authorizedKeysPath)
	return authorizedKeysPath
}

// This test is to verify the read and write operations of the authorized keys file
// does not modify the existing keys.
func TestReadWriteAuthorizedKeys(t *testing.T) {
	ctx := context.Background()
	// Generate some SSH key pairs.
	sshKeyPairs, err := generateSSHKeyPairs(t, 2048, 3)
	if err != nil {
		t.Fatalf("Failed to generate SSH key pairs: %v", err)
	}
	// Create the authorized keys file.
	authorizedKeysPath := createAuthorizedKeysFile(t, sshKeyPairs)
	handler := &RotateSSHKeyHandler{
		param: &pb.RotateSshKeyInput{
			RemoteTmp: t.TempDir(),
		},
	}
	// Retrieve the existing keys from the authorized keys file.
	retrievedKeys, err := handler.retrieveExistingKeys(ctx, authorizedKeysPath)
	if err != nil {
		t.Fatalf("Failed to retrieve existing keys: %v", err)
	}
	if len(retrievedKeys) != len(sshKeyPairs) {
		t.Fatalf("Expected %d existing keys, got %d", len(sshKeyPairs), len(retrievedKeys))
	}
	// Write the retrieved keys to a new authorized keys file.
	newAuthorizedKeysPath := filepath.Join(t.TempDir(), "authorized_keys")
	err = handler.writeAuthorizedKeys(ctx, nil, newAuthorizedKeysPath, retrievedKeys)
	if err != nil {
		t.Fatalf("Failed to write authorized keys: %v", err)
	}
	// Retrive the just written keys.
	newExistingKeys, err := handler.retrieveExistingKeys(ctx, newAuthorizedKeysPath)
	if err != nil {
		t.Fatalf("Failed to retrieve existing keys: %v", err)
	}
	if len(sshKeyPairs) != len(newExistingKeys) {
		t.Fatalf("Expected %d existing keys, got %d", len(sshKeyPairs), len(newExistingKeys))
	}
	// Compare with the original keys.
	for i := range retrievedKeys {
		existingKey := sshKeyPairs[i].publicKey
		if !bytes.Equal(existingKey, newExistingKeys[i]) {
			t.Fatalf("Expected key %d to be %s, got %s", i, existingKey, newExistingKeys[i])
		}
	}
}

// This test is to verify the rotate operation of the SSH key.
// It creates a new SSH key pair and rotates the SSH key.
// It then removes the first key and appends the new key.
// It then retrieves the updated keys and compares them with the original keys.
func TestRotateSshKey(t *testing.T) {
	ctx := context.Background()
	sshKeyPairs, err := generateSSHKeyPairs(t, 2048, 3)
	if err != nil {
		t.Fatalf("Failed to generate SSH key pairs: %v", err)
	}
	authorizedKeysPath := createAuthorizedKeysFile(t, sshKeyPairs)
	handler := &RotateSSHKeyHandler{
		param: &pb.RotateSshKeyInput{
			RemoteTmp: t.TempDir(),
		},
	}
	data, _ := os.ReadFile(authorizedKeysPath)
	t.Logf("\nBefore rotation: Authorized keys file contents:\n%s\n", string(data))
	// Generate a new SSH key pair.
	newSshKeyPairs, err := generateSSHKeyPairs(t, 2048, 1)
	if err != nil {
		t.Fatalf("Failed to generate new SSH key pairs: %v", err)
	}
	newPublicKey := newSshKeyPairs[0].publicKey
	oldPublicKey := sshKeyPairs[0].publicKey
	// Rotate the SSH key.
	err = handler.rotateKey(ctx, nil, authorizedKeysPath, oldPublicKey, newPublicKey)
	if err != nil {
		t.Fatalf("Failed to rotate SSH key: %v", err)
	}
	// Remove the first key and append the new key.
	sshKeyPairs = sshKeyPairs[1:]
	sshKeyPairs = append(sshKeyPairs, newSshKeyPairs[0])
	updatedKeys, err := handler.retrieveExistingKeys(ctx, authorizedKeysPath)
	if err != nil {
		t.Fatalf("Failed to retrieve existing keys: %v", err)
	}
	data, _ = os.ReadFile(authorizedKeysPath)
	t.Logf("\nAfter rotation: Authorized keys file contents:\n%s\n", string(data))
	if len(sshKeyPairs) != len(updatedKeys) {
		t.Fatalf("Expected %d existing keys, got %d", len(sshKeyPairs), len(updatedKeys))
	}
	for i := range updatedKeys {
		expectedKey := sshKeyPairs[i]
		if !bytes.Equal(expectedKey.publicKey, updatedKeys[i]) {
			t.Fatalf("Expected key %d to be %s, got %s", i, expectedKey.publicKey, updatedKeys[i])
		}
	}
}
