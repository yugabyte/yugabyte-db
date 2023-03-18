// Copyright (c) YugaByte, Inc.
package util

import (
	"context"
	"io/ioutil"
	"net/http"
	"testing"
)

func TestIsValidMethod(t *testing.T) {
	want, got := false, isValidMethod("test")
	if want != got {
		t.Fatalf("IsValid Method doesn't return expected value.")
	}

	want, got = true, isValidMethod(http.MethodDelete)
	if want != got {
		t.Fatalf("IsValid Method doesn't return expected value.")
	}
}

func TestValidate(t *testing.T) {
	queryParams := make(map[string]string)
	dummyData := `{ id: 123,
		data: dummyData
		}`
	got := validate(http.MethodGet, queryParams, dummyData)
	if got == nil {
		t.Fatalf("Expected a validation error but got success.")
	}

	got = validate(http.MethodPost, queryParams, dummyData)
	if got != nil {
		t.Fatalf("Expected a success but got validation error.")
	}
}

func TestHttpClientSuccess(t *testing.T) {
	config := CurrentConfig()
	testClient := NewHttpClient(
		100,
		config.String(PlatformUrlKey),
	)
	res, err := testClient.Do(context.TODO(), http.MethodGet, "/test", nil, nil, nil)
	if err != nil {
		t.Fatalf("Error while calling the request - %s", err.Error())
	}
	body, err := ioutil.ReadAll(res.Body)
	defer res.Body.Close()
	if err != nil {
		t.Fatalf("Error reading the respone - %s", err.Error())
	}
	if string(body) != "success" {
		t.Fatalf("Unexpected body in the response")
	}
	res, err = testClient.Do(context.TODO(), http.MethodPut, "/test", nil, nil, nil)
	if res.StatusCode != 405 {
		t.Fatalf("Expected 405 status code.")
	}
}
