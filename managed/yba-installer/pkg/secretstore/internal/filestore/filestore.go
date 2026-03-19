package filestore

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// fileStore is the file-based implementation of Store
type fileStore struct {
	filePath string
	secrets  map[string]string
	mu       sync.RWMutex
}

// newFileStore creates a new file-based store
func NewFileStore(filePath string) (*fileStore, error) {
	if filePath == "" {
		return nil, fmt.Errorf("file path cannot be empty")
	}
	fs := &fileStore{
		filePath: filePath,
		secrets:  make(map[string]string),
	}

	// Create directory if it doesn't exist
	dir := filepath.Dir(filePath)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return nil, fmt.Errorf("failed to create directory: %w", err)
	}

	// Load existing secrets if file exists
	if err := fs.load(); err != nil {
		// If file doesn't exist, that's okay - we'll create it on first write
		if !os.IsNotExist(err) {
			return nil, fmt.Errorf("failed to load secrets: %w", err)
		}
	}

	// Ensure file has correct permissions (600) if it exists
	if _, err := os.Stat(filePath); err == nil {
		if err := os.Chmod(filePath, 0600); err != nil {
			return nil, fmt.Errorf("failed to set permissions on secret store file: %w", err)
		}
	}

	return fs, nil
}

// load reads secrets from the file
func (fs *fileStore) load() error {
	fs.mu.Lock()
	defer fs.mu.Unlock()

	data, err := os.ReadFile(fs.filePath)
	if err != nil {
		return err
	}

	if len(data) == 0 {
		fs.secrets = make(map[string]string)
		return nil
	}

	if err := json.Unmarshal(data, &fs.secrets); err != nil {
		return fmt.Errorf("failed to unmarshal secrets: %w", err)
	}

	return nil
}

// save writes secrets to the file
// NOTE: This method should only be called while holding fs.mu write lock
func (fs *fileStore) save() error {
	data, err := json.Marshal(fs.secrets)
	if err != nil {
		return fmt.Errorf("failed to marshal secrets: %w", err)
	}

	// Write to temporary file first, then rename for atomicity
	tmpFile := fs.filePath + ".tmp"
	if err := os.WriteFile(tmpFile, data, 0600); err != nil {
		return fmt.Errorf("failed to write secrets: %w", err)
	}

	// Rename for atomic update
	if err := os.Rename(tmpFile, fs.filePath); err != nil {
		return fmt.Errorf("failed to rename secrets file: %w", err)
	}

	// Ensure permissions are correct
	if err := os.Chmod(fs.filePath, 0600); err != nil {
		log.Warn(fmt.Sprintf("failed to set permissions on secret store file: %v", err))
	}

	return nil
}

// Get retrieves a secret by key
func (fs *fileStore) Get(key string) (string, error) {
	fs.mu.RLock()
	defer fs.mu.RUnlock()

	secret, exists := fs.secrets[key]
	if !exists {
		return "", fmt.Errorf("secret not found for key: %s", key)
	}

	return secret, nil
}

// Set stores a secret by key
func (fs *fileStore) Set(key, secret string) error {
	fs.mu.Lock()
	defer fs.mu.Unlock()

	fs.secrets[key] = secret
	return fs.save()
}

// Delete removes a secret by key
func (fs *fileStore) Delete(key string) error {
	fs.mu.Lock()
	defer fs.mu.Unlock()

	if _, exists := fs.secrets[key]; !exists {
		return fmt.Errorf("secret not found for key: %s", key)
	}

	delete(fs.secrets, key)
	return fs.save()
}

// List returns all keys in the store
func (fs *fileStore) List() ([]string, error) {
	fs.mu.RLock()
	defer fs.mu.RUnlock()

	keys := make([]string, 0, len(fs.secrets))
	for key := range fs.secrets {
		keys = append(keys, key)
	}

	return keys, nil
}
