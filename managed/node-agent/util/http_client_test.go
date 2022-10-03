// Copyright (c) YugaByte, Inc.
package util

import (
	"io/ioutil"
	"net/http"
	"testing"
)

func TestIsValidMethod(t *testing.T) {
	want, got := false, isValidMethod("test")
	if want != got {
		t.Errorf("IsValid Method doesn't return expected value.")
	}

	want, got = true, isValidMethod(http.MethodDelete)
	if want != got {
		t.Errorf("IsValid Method doesn't return expected value.")
	}
}

func TestValidate(t *testing.T) {
	queryParams := make(map[string]string)
	dummyData := `{ id: 123,
		data: dummyData
		}`
	got := validate(http.MethodGet, queryParams, dummyData)
	if got == nil {
		t.Errorf("Expected a validation error but got success.")
	}

	got = validate(http.MethodPost, queryParams, dummyData)
	if got != nil {
		t.Errorf("Expected a success but got validation error.")
	}
}

func TestHttpClientSuccess(t *testing.T) {
	config := CurrentConfig()
	testClient := NewHttpClient(
		100,
		config.String(PlatformUrlKey),
	)
	res, err := testClient.Do(http.MethodGet, "/test", nil, nil, nil)
	if err != nil {
		t.Errorf("Error while calling the request.")
	}
	body, err := ioutil.ReadAll(res.Body)
	defer res.Body.Close()
	if err != nil {
		t.Errorf("Error reading the respone.")
	}
	if string(body) != "success" {
		t.Errorf("Unexpected body in the response")
	}
	res, err = testClient.Do(http.MethodPut, "/test", nil, nil, nil)
	if res.StatusCode != 405 {
		t.Errorf("Expected 405 status code.")
	}
}
