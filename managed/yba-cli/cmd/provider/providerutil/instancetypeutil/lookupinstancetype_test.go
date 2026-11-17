/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetypeutil

import (
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"reflect"
	"strings"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
)

func TestLookupInstanceType(t *testing.T) {
	const shape = "Standard_Test"
	tests := []struct {
		name         string
		providerType string
		detail       int
		detailBody   string
		listBody     string
		wantFound    bool
		wantCores    float64
		wantCodes    []string
		wantErr      bool
	}{
		{
			name:       "active type",
			detail:     http.StatusOK,
			detailBody: `{"idKey":{"instanceTypeCode":"` + shape + `"},"active":true,"numCores":2}`,
			wantFound:  true,
			wantCores:  2,
		},
		{
			name:   "active type with cloud instance type codes",
			detail: http.StatusOK,
			detailBody: `{"idKey":{"instanceTypeCode":"` + shape + `"},"active":true,"numCores":2,` +
				`"instanceTypeDetails":{"cloudInstanceTypeCodes":["Standard_D4s_v5"]}}`,
			wantFound: true,
			wantCores: 2,
			wantCodes: []string{"Standard_D4s_v5"},
		},
		{
			name:       "inactive type is new",
			detail:     http.StatusOK,
			detailBody: `{"idKey":{"instanceTypeCode":"` + shape + `"},"active":false,"numCores":2}`,
		},
		{
			name:       "missing type is new",
			detail:     http.StatusBadRequest,
			detailBody: `{"success":false,"error":"Instance type not found: ` + shape + `"}`,
		},
		{
			name:   "detail error falls back to the list",
			detail: http.StatusInternalServerError,
			listBody: `[{"idKey":{"instanceTypeCode":"other"},"numCores":8},` +
				`{"idKey":{"instanceTypeCode":"` + shape + `"},"active":true,"numCores":2,` +
				`"instanceTypeDetails":null}]`,
			wantFound: true,
			wantCores: 2,
		},
		{
			name:   "list fallback reads cloud instance type codes",
			detail: http.StatusInternalServerError,
			listBody: `[{"idKey":{"instanceTypeCode":"` + shape + `"},"active":true,"numCores":2,` +
				`"instanceTypeDetails":{"cloudInstanceTypeCodes":["Standard_D4s_v5"]}}]`,
			wantFound: true,
			wantCores: 2,
			wantCodes: []string{"Standard_D4s_v5"},
		},
		{
			name:     "detail error and type not in the list fails",
			detail:   http.StatusInternalServerError,
			listBody: `[{"idKey":{"instanceTypeCode":"other"},"numCores":8}]`,
			wantErr:  true,
		},
		{
			name:     "detail error and unreadable list fails",
			detail:   http.StatusInternalServerError,
			listBody: `not json`,
			wantErr:  true,
		},
		{
			name:         "detail error on aws fails without the list",
			providerType: util.AWSProviderType,
			detail:       http.StatusInternalServerError,
			listBody:     `[{"idKey":{"instanceTypeCode":"` + shape + `"},"active":true,"numCores":2}]`,
			wantErr:      true,
		},
		{
			name:       "detail error other than 5xx fails",
			detail:     http.StatusForbidden,
			detailBody: `{"success":false,"error":"forbidden"}`,
			wantErr:    true,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(
				func(w http.ResponseWriter, r *http.Request) {
					w.Header().Set("Content-Type", "application/json")
					if strings.HasSuffix(r.URL.Path, "/instance_types") {
						w.Write([]byte(tc.listBody))
						return
					}
					w.WriteHeader(tc.detail)
					w.Write([]byte(tc.detailBody))
				}))
			defer server.Close()
			serverURL, err := url.Parse(server.URL)
			if err != nil {
				t.Fatal(err)
			}
			authAPI, err := ybaAuthClient.NewAuthAPIClientInitialize(serverURL, "token")
			if err != nil {
				t.Fatal(err)
			}

			providerType := tc.providerType
			if providerType == "" {
				providerType = util.AzureProviderType
			}
			got, codes, err := lookupInstanceType(
				authAPI, "provider-uuid", providerType, shape, "test")
			if tc.wantErr {
				if err == nil {
					t.Fatalf("got %+v, want an error", got)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if (got != nil) != tc.wantFound {
				t.Fatalf("found = %v, want %v", got != nil, tc.wantFound)
			}
			if got != nil && got.GetNumCores() != tc.wantCores {
				t.Errorf("cores = %v, want %v", got.GetNumCores(), tc.wantCores)
			}
			if !reflect.DeepEqual(codes, tc.wantCodes) {
				t.Errorf("cloud codes = %v, want %v", codes, tc.wantCodes)
			}
		})
	}
}

type failingReader struct{}

func (failingReader) Read([]byte) (int, error) { return 0, errors.New("read failed") }

func TestDecodeBody(t *testing.T) {
	var v storedInstanceType
	tests := []struct {
		name     string
		response *http.Response
		wantErr  bool
	}{
		{name: "nil response", wantErr: true},
		{name: "nil body", response: &http.Response{}, wantErr: true},
		{
			name:     "read error",
			response: &http.Response{Body: io.NopCloser(failingReader{})},
			wantErr:  true,
		},
		{
			name:     "bad json",
			response: &http.Response{Body: io.NopCloser(strings.NewReader("{"))},
			wantErr:  true,
		},
		{
			name:     "empty body",
			response: &http.Response{Body: io.NopCloser(strings.NewReader(""))},
			wantErr:  true,
		},
		{
			name: "valid body",
			response: &http.Response{Body: io.NopCloser(strings.NewReader(
				`{"instanceTypeDetails":{"cloudInstanceTypeCodes":["a"]}}`))},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := decodeBody(tc.response, &v)
			if (err != nil) != tc.wantErr {
				t.Fatalf("err = %v, want error %v", err, tc.wantErr)
			}
		})
	}
}
