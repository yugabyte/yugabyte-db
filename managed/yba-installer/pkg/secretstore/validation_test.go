/*
 * Copyright (c) YugabyteDB, Inc.
 */

package secretstore

import (
	"testing"
)

func TestValidatePassword(t *testing.T) {
	t.Run("empty password", func(t *testing.T) {
		opts := passwordOptions{}
		err := validatePassword("", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail for empty password")
		}
	})

	t.Run("no requirements", func(t *testing.T) {
		opts := passwordOptions{}
		err := validatePassword("anypassword", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}
	})

	t.Run("min length valid", func(t *testing.T) {
		opts := passwordOptions{MinLength: 8}
		err := validatePassword("password", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}
	})

	t.Run("min length invalid", func(t *testing.T) {
		opts := passwordOptions{MinLength: 8}
		err := validatePassword("short", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail for password too short")
		}
	})

	t.Run("max length valid", func(t *testing.T) {
		opts := passwordOptions{MaxLength: 20}
		err := validatePassword("password", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}
	})

	t.Run("max length invalid", func(t *testing.T) {
		opts := passwordOptions{MaxLength: 5}
		err := validatePassword("toolongpassword", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail for password too long")
		}
	})

	t.Run("min and max length", func(t *testing.T) {
		opts := passwordOptions{MinLength: 5, MaxLength: 10}
		err := validatePassword("validpass", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}

		err = validatePassword("shor", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail for password too short")
		}

		err = validatePassword("toolongpassword", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail for password too long")
		}
	})

	t.Run("require uppercase valid", func(t *testing.T) {
		opts := passwordOptions{RequireUppercase: true}
		err := validatePassword("Password", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}
	})

	t.Run("require uppercase invalid", func(t *testing.T) {
		opts := passwordOptions{RequireUppercase: true}
		err := validatePassword("password", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail without uppercase")
		}
	})

	t.Run("require lowercase valid", func(t *testing.T) {
		opts := passwordOptions{RequireLowercase: true}
		err := validatePassword("password", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}
	})

	t.Run("require lowercase invalid", func(t *testing.T) {
		opts := passwordOptions{RequireLowercase: true}
		err := validatePassword("PASSWORD", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail without lowercase")
		}
	})

	t.Run("require digit valid", func(t *testing.T) {
		opts := passwordOptions{RequireDigit: true}
		err := validatePassword("password1", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}
	})

	t.Run("require digit invalid", func(t *testing.T) {
		opts := passwordOptions{RequireDigit: true}
		err := validatePassword("password", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail without digit")
		}
	})

	t.Run("require special char valid", func(t *testing.T) {
		opts := passwordOptions{RequireSpecialChar: true}
		err := validatePassword("password!", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}
	})

	t.Run("require special char invalid", func(t *testing.T) {
		opts := passwordOptions{RequireSpecialChar: true}
		err := validatePassword("password", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail without special char")
		}
	})

	t.Run("all requirements valid", func(t *testing.T) {
		opts := passwordOptions{
			MinLength:         8,
			MaxLength:         20,
			RequireUppercase:  true,
			RequireLowercase:  true,
			RequireDigit:       true,
			RequireSpecialChar: true,
		}
		err := validatePassword("SecurePass123!", opts)
		if err != nil {
			t.Errorf("validatePassword() failed: %v", err)
		}
	})

	t.Run("all requirements invalid - missing uppercase", func(t *testing.T) {
		opts := passwordOptions{
			MinLength:         8,
			RequireUppercase:  true,
			RequireLowercase:  true,
			RequireDigit:       true,
			RequireSpecialChar: true,
		}
		err := validatePassword("securepass123!", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail without uppercase")
		}
	})

	t.Run("all requirements invalid - missing lowercase", func(t *testing.T) {
		opts := passwordOptions{
			MinLength:         8,
			RequireUppercase:  true,
			RequireLowercase:  true,
			RequireDigit:       true,
			RequireSpecialChar: true,
		}
		err := validatePassword("SECUREPASS123!", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail without lowercase")
		}
	})

	t.Run("all requirements invalid - missing digit", func(t *testing.T) {
		opts := passwordOptions{
			MinLength:         8,
			RequireUppercase:  true,
			RequireLowercase:  true,
			RequireDigit:       true,
			RequireSpecialChar: true,
		}
		err := validatePassword("SecurePass!", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail without digit")
		}
	})

	t.Run("all requirements invalid - missing special char", func(t *testing.T) {
		opts := passwordOptions{
			MinLength:         8,
			RequireUppercase:  true,
			RequireLowercase:  true,
			RequireDigit:       true,
			RequireSpecialChar: true,
		}
		err := validatePassword("SecurePass123", opts)
		if err == nil {
			t.Fatal("validatePassword() should fail without special char")
		}
	})

	t.Run("special characters", func(t *testing.T) {
		opts := passwordOptions{RequireSpecialChar: true}
		specialChars := []string{"!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "-", "_", "+", "=", "[", "]", "{", "}", "|", "\\", ":", ";", "\"", "'", "<", ">", ",", ".", "?", "/", "~", "`"}

		for _, char := range specialChars {
			err := validatePassword("password"+char, opts)
			if err != nil {
				t.Errorf("validatePassword() failed for special char %q: %v", char, err)
			}
		}
	})

	t.Run("unicode characters", func(t *testing.T) {
		opts := passwordOptions{MinLength: 1}
		err := validatePassword("密码", opts)
		if err != nil {
			t.Errorf("validatePassword() failed for unicode: %v", err)
		}
	})
}

