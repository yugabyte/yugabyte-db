/*
 * Copyright (c) YugabyteDB, Inc.
 */

package secretstore

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestNew(t *testing.T) {
	tmpDir := t.TempDir()
	filePath := filepath.Join(tmpDir, "secrets.json")

	t.Run("successful creation", func(t *testing.T) {
		store, err := NewDefaultStore(filePath)
		if err != nil {
			t.Fatalf("New() failed: %v", err)
		}
		if store == nil {
			t.Fatal("New() returned nil store")
		}
		if store.store == nil {
			t.Fatal("store.store is nil")
		}
	})

	t.Run("empty file path", func(t *testing.T) {
		_, err := NewDefaultStore("")
		if err == nil {
			t.Fatal("NewDefaultStore() should fail with empty file path")
		}
	})

	t.Run("creates directory if not exists", func(t *testing.T) {
		tmpDir := t.TempDir()
		filePath := filepath.Join(tmpDir, "subdir", "secrets.json")
		store, err := NewDefaultStore(filePath)
		if err != nil {
			t.Fatalf("New() failed: %v", err)
		}
		if store == nil {
			t.Fatal("New() returned nil store")
		}
		// Verify directory was created
		if _, err := os.Stat(filepath.Dir(filePath)); os.IsNotExist(err) {
			t.Fatal("Directory was not created")
		}
	})
}

func TestSecretStore_Get(t *testing.T) {
	tmpDir := t.TempDir()
	filePath := filepath.Join(tmpDir, "secrets.json")
	store, err := NewDefaultStore(filePath)
	if err != nil {
		t.Fatalf("New() failed: %v", err)
	}

	t.Run("get existing secret", func(t *testing.T) {
		key := "test_key"
		secret := "test_secret"
		if err := store.Set(key, secret); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		result, err := store.Get(key)
		if err != nil {
			t.Fatalf("Get() failed: %v", err)
		}
		if result != secret {
			t.Errorf("Get() = %q, want %q", result, secret)
		}
	})

	t.Run("get non-existent secret", func(t *testing.T) {
		_, err := store.Get("non_existent_key")
		if err == nil {
			t.Fatal("Get() should fail for non-existent key")
		}
	})

	t.Run("empty key", func(t *testing.T) {
		_, err := store.Get("")
		if err == nil {
			t.Fatal("Get() should fail with empty key")
		}
	})
}

func TestSecretStore_Set(t *testing.T) {
	tmpDir := t.TempDir()
	filePath := filepath.Join(tmpDir, "secrets.json")
	store, err := NewDefaultStore(filePath)
	if err != nil {
		t.Fatalf("New() failed: %v", err)
	}

	t.Run("set without options", func(t *testing.T) {
		key := "key1"
		secret := "password123"
		if err := store.Set(key, secret); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		result, err := store.Get(key)
		if err != nil {
			t.Fatalf("Get() failed: %v", err)
		}
		if result != secret {
			t.Errorf("Get() = %q, want %q", result, secret)
		}
	})

	t.Run("set with min length", func(t *testing.T) {
		key := "key2"
		secret := "password123"
		if err := store.Set(key, secret, WithMinLength(8)); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		// Try with too short password
		if err := store.Set("key3", "short", WithMinLength(8)); err == nil {
			t.Fatal("Set() should fail with password too short")
		}
	})

	t.Run("set with max length", func(t *testing.T) {
		key := "key4"
		secret := "pass"
		if err := store.Set(key, secret, WithMaxLength(10)); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		// Try with too long password
		if err := store.Set("key5", "thispasswordistoolong", WithMaxLength(10)); err == nil {
			t.Fatal("Set() should fail with password too long")
		}
	})

	t.Run("set with uppercase requirement", func(t *testing.T) {
		key := "key6"
		secret := "Password123"
		if err := store.Set(key, secret, WithRequireUppercase()); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		// Try without uppercase
		if err := store.Set("key7", "password123", WithRequireUppercase()); err == nil {
			t.Fatal("Set() should fail without uppercase")
		}
	})

	t.Run("set with lowercase requirement", func(t *testing.T) {
		key := "key8"
		secret := "password123"
		if err := store.Set(key, secret, WithRequireLowercase()); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		// Try without lowercase
		if err := store.Set("key9", "PASSWORD123", WithRequireLowercase()); err == nil {
			t.Fatal("Set() should fail without lowercase")
		}
	})

	t.Run("set with digit requirement", func(t *testing.T) {
		key := "key10"
		secret := "Password123"
		if err := store.Set(key, secret, WithRequireDigit()); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		// Try without digit
		if err := store.Set("key11", "Password", WithRequireDigit()); err == nil {
			t.Fatal("Set() should fail without digit")
		}
	})

	t.Run("set with special char requirement", func(t *testing.T) {
		key := "key12"
		secret := "Password123!"
		if err := store.Set(key, secret, WithRequireSpecialChar()); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		// Try without special char
		if err := store.Set("key13", "Password123", WithRequireSpecialChar()); err == nil {
			t.Fatal("Set() should fail without special char")
		}
	})

	t.Run("set with multiple options", func(t *testing.T) {
		key := "key14"
		secret := "SecurePass123!"
		if err := store.Set(key, secret,
			WithMinLength(8),
			WithRequireUppercase(),
			WithRequireLowercase(),
			WithRequireDigit(),
			WithRequireSpecialChar(),
		); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}
	})

	t.Run("empty key", func(t *testing.T) {
		if err := store.Set("", "password"); err == nil {
			t.Fatal("Set() should fail with empty key")
		}
	})

	t.Run("empty password", func(t *testing.T) {
		if err := store.Set("key15", ""); err == nil {
			t.Fatal("Set() should fail with empty password")
		}
	})
}

func TestSecretStore_Delete(t *testing.T) {
	tmpDir := t.TempDir()
	filePath := filepath.Join(tmpDir, "secrets.json")
	store, err := NewDefaultStore(filePath)
	if err != nil {
		t.Fatalf("New() failed: %v", err)
	}

	t.Run("delete existing secret", func(t *testing.T) {
		key := "delete_key"
		secret := "secret"
		if err := store.Set(key, secret); err != nil {
			t.Fatalf("Set() failed: %v", err)
		}

		if err := store.Delete(key); err != nil {
			t.Fatalf("Delete() failed: %v", err)
		}

		// Verify it's deleted
		_, err := store.Get(key)
		if err == nil {
			t.Fatal("Get() should fail after Delete()")
		}
	})

	t.Run("delete non-existent secret", func(t *testing.T) {
		if err := store.Delete("non_existent"); err == nil {
			t.Fatal("Delete() should fail for non-existent key")
		}
	})

	t.Run("empty key", func(t *testing.T) {
		if err := store.Delete(""); err == nil {
			t.Fatal("Delete() should fail with empty key")
		}
	})
}

func TestSecretStore_List(t *testing.T) {
	tmpDir := t.TempDir()
	filePath := filepath.Join(tmpDir, "secrets.json")
	store, err := NewDefaultStore(filePath)
	if err != nil {
		t.Fatalf("New() failed: %v", err)
	}

	t.Run("list empty store", func(t *testing.T) {
		keys, err := store.List()
		if err != nil {
			t.Fatalf("List() failed: %v", err)
		}
		if len(keys) != 0 {
			t.Errorf("List() = %v, want empty slice", keys)
		}
	})

	t.Run("list with secrets", func(t *testing.T) {
		// Add some secrets
		secrets := map[string]string{
			"key1": "secret1",
			"key2": "secret2",
			"key3": "secret3",
		}

		for key, secret := range secrets {
			if err := store.Set(key, secret); err != nil {
				t.Fatalf("Set() failed: %v", err)
			}
		}

		keys, err := store.List()
		if err != nil {
			t.Fatalf("List() failed: %v", err)
		}

		if len(keys) != len(secrets) {
			t.Errorf("List() returned %d keys, want %d", len(keys), len(secrets))
		}

		// Check all keys are present
		keyMap := make(map[string]bool)
		for _, key := range keys {
			keyMap[key] = true
		}

		for key := range secrets {
			if !keyMap[key] {
				t.Errorf("List() missing key: %s", key)
			}
		}
	})
}

func TestFilePermissions(t *testing.T) {
	tmpDir := t.TempDir()
	filePath := filepath.Join(tmpDir, "secrets.json")
	store, err := NewDefaultStore(filePath)
	if err != nil {
		t.Fatalf("New() failed: %v", err)
	}

	// Set a secret to create the file
	if err := store.Set("test_key", "test_secret"); err != nil {
		t.Fatalf("Set() failed: %v", err)
	}

	// Check file permissions
	info, err := os.Stat(filePath)
	if err != nil {
		t.Fatalf("Stat() failed: %v", err)
	}

	// Check that permissions are 0600 (rw-------)
	expectedPerms := os.FileMode(0600)
	actualPerms := info.Mode().Perm()
	if actualPerms != expectedPerms {
		t.Errorf("File permissions = %o, want %o", actualPerms, expectedPerms)
	}
}

func TestPersistence(t *testing.T) {
	tmpDir := t.TempDir()
	filePath := filepath.Join(tmpDir, "secrets.json")

	// Create first store and set a secret
	store1, err := NewDefaultStore(filePath)
	if err != nil {
		t.Fatalf("New() failed: %v", err)
	}

	key := "persistent_key"
	secret := "persistent_secret"
	if err := store1.Set(key, secret); err != nil {
		t.Fatalf("Set() failed: %v", err)
	}

	// Create a new store instance pointing to the same file
	store2, err := NewDefaultStore(filePath)
	if err != nil {
		t.Fatalf("New() failed: %v", err)
	}

	// Verify the secret persists
	result, err := store2.Get(key)
	if err != nil {
		t.Fatalf("Get() failed: %v", err)
	}
	if result != secret {
		t.Errorf("Get() = %q, want %q", result, secret)
	}
}

func TestConcurrentAccess(t *testing.T) {
	tmpDir := t.TempDir()
	filePath := filepath.Join(tmpDir, "secrets.json")
	store, err := NewDefaultStore(filePath)
	if err != nil {
		t.Fatalf("New() failed: %v", err)
	}

	// Test concurrent writes
	done := make(chan bool, 10)
	for i := 0; i < 10; i++ {
		go func(i int) {
			key := "concurrent_key"
			secret := "secret"
			if err := store.Set(key, secret); err != nil {
				t.Errorf("Set() failed: %v", err)
			}
			done <- true
		}(i)
	}

	// Wait for all goroutines
	for i := 0; i < 10; i++ {
		<-done
	}

	// Verify final state
	result, err := store.Get("concurrent_key")
	if err != nil {
		t.Fatalf("Get() failed: %v", err)
	}
	if result != "secret" {
		t.Errorf("Get() = %q, want %q", result, "secret")
	}
}

func TestPasswordOptions(t *testing.T) {
	t.Run("WithMinLength", func(t *testing.T) {
		opts := applyOptions(WithMinLength(8))
		if opts.MinLength != 8 {
			t.Errorf("MinLength = %d, want 8", opts.MinLength)
		}
	})

	t.Run("WithMaxLength", func(t *testing.T) {
		opts := applyOptions(WithMaxLength(20))
		if opts.MaxLength != 20 {
			t.Errorf("MaxLength = %d, want 20", opts.MaxLength)
		}
	})

	t.Run("WithRequireUppercase", func(t *testing.T) {
		opts := applyOptions(WithRequireUppercase())
		if !opts.RequireUppercase {
			t.Error("RequireUppercase should be true")
		}
	})

	t.Run("WithRequireLowercase", func(t *testing.T) {
		opts := applyOptions(WithRequireLowercase())
		if !opts.RequireLowercase {
			t.Error("RequireLowercase should be true")
		}
	})

	t.Run("WithRequireDigit", func(t *testing.T) {
		opts := applyOptions(WithRequireDigit())
		if !opts.RequireDigit {
			t.Error("RequireDigit should be true")
		}
	})

	t.Run("WithRequireSpecialChar", func(t *testing.T) {
		opts := applyOptions(WithRequireSpecialChar())
		if !opts.RequireSpecialChar {
			t.Error("RequireSpecialChar should be true")
		}
	})

	t.Run("multiple options", func(t *testing.T) {
		opts := applyOptions(
			WithMinLength(10),
			WithMaxLength(20),
			WithRequireUppercase(),
			WithRequireLowercase(),
			WithRequireDigit(),
			WithRequireSpecialChar(),
		)

		if opts.MinLength != 10 {
			t.Errorf("MinLength = %d, want 10", opts.MinLength)
		}
		if opts.MaxLength != 20 {
			t.Errorf("MaxLength = %d, want 20", opts.MaxLength)
		}
		if !opts.RequireUppercase {
			t.Error("RequireUppercase should be true")
		}
		if !opts.RequireLowercase {
			t.Error("RequireLowercase should be true")
		}
		if !opts.RequireDigit {
			t.Error("RequireDigit should be true")
		}
		if !opts.RequireSpecialChar {
			t.Error("RequireSpecialChar should be true")
		}
	})

	t.Run("no options", func(t *testing.T) {
		opts := applyOptions()
		emptyOpts := passwordOptions{}
		if !reflect.DeepEqual(opts, emptyOpts) {
			t.Errorf("applyOptions() = %+v, want %+v", opts, emptyOpts)
		}
	})
}
