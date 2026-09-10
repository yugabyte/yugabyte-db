// Copyright (c) YugabyteDB, Inc.

package server

import (
	"context"
	"node-agent/util"
	"testing"
)

func TestValidateNodeAgentIfExistsMatchingEmptyCertificate(t *testing.T) {
	util.MockNodeAgentCertificateUuid = ""
	defer func() { util.MockNodeAgentCertificateUuid = "" }()

	err := ValidateNodeAgentIfExists(context.Background(), "123456", "")
	if err != nil {
		t.Fatalf("Expected existing node agent to match empty certificate, got %v", err)
	}
}

func TestValidateNodeAgentIfExistsMatchingCertificateName(t *testing.T) {
	util.MockNodeAgentCertificateUuid = util.DummyCertificateUuid
	defer func() { util.MockNodeAgentCertificateUuid = "" }()

	err := ValidateNodeAgentIfExists(
		context.Background(),
		"123456",
		util.DummyCertificateName,
	)
	if err != nil {
		t.Fatalf("Expected existing node agent to match certificate name, got %v", err)
	}
}

func TestValidateNodeAgentIfExistsCertificateMismatch(t *testing.T) {
	util.MockNodeAgentCertificateUuid = "other-cert-uuid"
	defer func() { util.MockNodeAgentCertificateUuid = "" }()

	err := ValidateNodeAgentIfExists(
		context.Background(),
		"123456",
		util.DummyCertificateName,
	)
	if err != util.ErrNotExist {
		t.Fatalf("Expected ErrNotExist to trigger re-register, got %v", err)
	}
}

func TestValidateNodeAgentIfExistsEmptyToNamedCertificate(t *testing.T) {
	util.MockNodeAgentCertificateUuid = ""
	defer func() { util.MockNodeAgentCertificateUuid = "" }()

	err := ValidateNodeAgentIfExists(
		context.Background(),
		"123456",
		util.DummyCertificateName,
	)
	if err != util.ErrNotExist {
		t.Fatalf("Expected ErrNotExist when switching to named certificate, got %v", err)
	}
}
