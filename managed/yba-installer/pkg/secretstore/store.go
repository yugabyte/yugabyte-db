/*
 * Copyright (c) YugabyteDB, Inc.
 */

package secretstore

import (
	"errors"
	"fmt"
	"sync"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/secretstore/internal/filestore"
)

// Store is the interface for underlying storage backends
type Store interface {
	// Get retrieves a secret by key
	Get(key string) (string, error)
	// Set stores a secret by key
	Set(key, secret string) error
	// Delete removes a secret by key
	Delete(key string) error
	// List returns all keys in the store
	List() ([]string, error)
}

// passwordOptions defines requirements for password validation
type passwordOptions struct {
	// MinLength is the minimum password length (0 means no minimum)
	MinLength int
	// MaxLength is the maximum password length (0 means no maximum)
	MaxLength int
	// RequireUppercase requires at least one uppercase letter
	RequireUppercase bool
	// RequireLowercase requires at least one lowercase letter
	RequireLowercase bool
	// RequireDigit requires at least one digit
	RequireDigit bool
	// RequireSpecialChar requires at least one special character
	RequireSpecialChar bool
}

// PasswordOption is a function that modifies PasswordOptions
type PasswordOption func(*passwordOptions)

// applyOptions applies the given options to a PasswordOptions struct
func applyOptions(opts ...PasswordOption) passwordOptions {
	options := passwordOptions{}
	for _, opt := range opts {
		opt(&options)
	}
	return options
}

// WithMinLength sets the minimum password length
func WithMinLength(length int) PasswordOption {
	return func(opts *passwordOptions) {
		opts.MinLength = length
	}
}

// WithMaxLength sets the maximum password length
func WithMaxLength(length int) PasswordOption {
	return func(opts *passwordOptions) {
		opts.MaxLength = length
	}
}

// WithRequireUppercase requires at least one uppercase letter
func WithRequireUppercase() PasswordOption {
	return func(opts *passwordOptions) {
		opts.RequireUppercase = true
	}
}

// WithRequireLowercase requires at least one lowercase letter
func WithRequireLowercase() PasswordOption {
	return func(opts *passwordOptions) {
		opts.RequireLowercase = true
	}
}

// WithRequireDigit requires at least one digit
func WithRequireDigit() PasswordOption {
	return func(opts *passwordOptions) {
		opts.RequireDigit = true
	}
}

// WithRequireSpecialChar requires at least one special character
func WithRequireSpecialChar() PasswordOption {
	return func(opts *passwordOptions) {
		opts.RequireSpecialChar = true
	}
}

// SecretStore is the main interface for the secret store
type SecretStore struct {
	store Store
	mu    sync.RWMutex
}

func NewDefaultStore(filePath string) (*SecretStore, error) {
	store, err := filestore.NewFileStore(filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to create file store: %w", err)
	}
	return New(store), nil
}

func New(store Store) *SecretStore {
	return &SecretStore{store: store}
}

// Get retrieves a password by key
func (s *SecretStore) Get(key string) (string, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	if key == "" {
		return "", errors.New("key cannot be empty")
	}

	return s.store.Get(key)
}

// Set stores a password with the given key and secret, validating against options
func (s *SecretStore) Set(key, secret string, options ...PasswordOption) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if key == "" {
		return errors.New("key cannot be empty")
	}

	opts := applyOptions(options...)
	if err := validatePassword(secret, opts); err != nil {
		return fmt.Errorf("password validation failed: %w", err)
	}

	return s.store.Set(key, secret)
}

// SetWithPrompt prompts the user for a password and stores it with the given key
func (s *SecretStore) SetWithPrompt(key string, options ...PasswordOption) error {
	if key == "" {
		return errors.New("key cannot be empty")
	}

	opts := applyOptions(options...)

	// Prompt for password before acquiring lock to avoid blocking other operations
	secret, err := promptPassword(opts)
	if err != nil {
		return fmt.Errorf("failed to prompt for password: %w", err)
	}

	// Validate password (this is also done in promptPassword, but validate again for safety)
	if err := validatePassword(secret, opts); err != nil {
		return fmt.Errorf("password validation failed: %w", err)
	}

	// Now acquire lock and store
	s.mu.Lock()
	defer s.mu.Unlock()

	return s.store.Set(key, secret)
}

// Delete removes a secret by key
func (s *SecretStore) Delete(key string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if key == "" {
		return errors.New("key cannot be empty")
	}

	return s.store.Delete(key)
}

// List returns all keys in the store
func (s *SecretStore) List() ([]string, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	return s.store.List()
}
