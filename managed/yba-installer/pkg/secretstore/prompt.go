/*
 * Copyright (c) YugabyteDB, Inc.
 */

package secretstore

import (
	"bufio"
	"fmt"
	"os"
	"strings"
	"syscall"

	"golang.org/x/term"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// promptPassword prompts the user for a password with validation
func promptPassword(options passwordOptions) (string, error) {
	var password string
	var err error

	for {
		// Prompt for password
		fmt.Print("Enter password: ")
		password, err = readPassword()
		if err != nil {
			return "", fmt.Errorf("failed to read password: %w", err)
		}
		fmt.Println() // New line after password input

		// Validate password
		if err := validatePassword(password, options); err != nil {
			log.Error(fmt.Sprintf("Password validation failed: %v", err))
			fmt.Println("Please try again.")
			continue
		}

		// Confirm password
		fmt.Print("Confirm password: ")
		confirmPassword, err := readPassword()
		if err != nil {
			return "", fmt.Errorf("failed to read password confirmation: %w", err)
		}
		fmt.Println() // New line after password input

		if password != confirmPassword {
			log.Error("Passwords do not match")
			fmt.Println("Passwords do not match. Please try again.")
			continue
		}

		return password, nil
	}
}

// readPassword reads a password from stdin without echoing it
func readPassword() (string, error) {
	// Check if stdin is a terminal
	if !term.IsTerminal(int(syscall.Stdin)) {
		// If not a terminal, read from stdin normally (for testing/automation)
		reader := bufio.NewReader(os.Stdin)
		password, err := reader.ReadString('\n')
		if err != nil {
			return "", err
		}
		return strings.TrimSpace(password), nil
	}

	// Read password without echoing
	passwordBytes, err := term.ReadPassword(int(syscall.Stdin))
	if err != nil {
		return "", err
	}

	return string(passwordBytes), nil
}
