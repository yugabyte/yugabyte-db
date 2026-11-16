// Copyright (c) YugabyteDB, Inc.

package serverconfredact

import (
	"encoding/base64"
	"fmt"
)

func decodeBase64String(encoded string) (string, error) {
	data, err := base64.StdEncoding.DecodeString(encoded)
	if err != nil {
		return "", fmt.Errorf("invalid base64: %w", err)
	}
	return string(data), nil
}
