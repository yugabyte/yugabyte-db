/*
 * Copyright (c) YugabyteDB, Inc.
 */

package secretstore

import (
	"errors"
	"fmt"
	"unicode"
)

// validatePassword validates a password against the given options
func validatePassword(password string, options passwordOptions) error {
	if password == "" {
		return errors.New("password cannot be empty")
	}

	// Check minimum length
	if options.MinLength > 0 && len(password) < options.MinLength {
		return fmt.Errorf("password must be at least %d characters long", options.MinLength)
	}

	// Check maximum length
	if options.MaxLength > 0 && len(password) > options.MaxLength {
		return fmt.Errorf("password must be at most %d characters long", options.MaxLength)
	}

	// Check character requirements
	hasUpper := false
	hasLower := false
	hasDigit := false
	hasSpecial := false

	for _, char := range password {
		if unicode.IsUpper(char) {
			hasUpper = true
		}
		if unicode.IsLower(char) {
			hasLower = true
		}
		if unicode.IsDigit(char) {
			hasDigit = true
		}
		if !unicode.IsLetter(char) && !unicode.IsDigit(char) && !unicode.IsSpace(char) {
			hasSpecial = true
		}
	}

	if options.RequireUppercase && !hasUpper {
		return errors.New("password must contain at least one uppercase letter")
	}

	if options.RequireLowercase && !hasLower {
		return errors.New("password must contain at least one lowercase letter")
	}

	if options.RequireDigit && !hasDigit {
		return errors.New("password must contain at least one digit")
	}

	if options.RequireSpecialChar && !hasSpecial {
		return errors.New("password must contain at least one special character")
	}

	return nil
}

