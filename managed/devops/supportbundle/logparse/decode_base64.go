// Copyright (c) YugabyteDB, Inc.

package logparse

import (
	"encoding/base64"
	"fmt"
	"strings"
)

// DecodeBase64String decodes a base64-encoded UTF-8 string (mirrors decode_base64_string in
// support_bundle_utils.sh).
func DecodeBase64String(encoded string) (string, error) {
	encoded = strings.TrimSpace(encoded)
	if encoded == "" {
		return "", nil
	}
	decoded, err := base64.StdEncoding.DecodeString(encoded)
	if err != nil {
		return "", fmt.Errorf("base64 decode: %w", err)
	}
	return string(decoded), nil
}
