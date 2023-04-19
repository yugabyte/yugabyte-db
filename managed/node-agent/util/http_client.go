// Copyright (c) YugaByte, Inc.
package util

import (
	"bytes"
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"sync"
	"time"
)

var (
	once     sync.Once
	certPool *x509.CertPool
)

type HttpClient struct {
	client *http.Client
	url    string
}

func NewHttpClient(timeout int, url string) *HttpClient {
	config := CurrentConfig()
	once.Do(func() {
		caCertPath := config.String(PlatformCaCertPathKey)
		if caCertPath != "" {
			certPool := x509.NewCertPool()
			pem, err := os.ReadFile(caCertPath)
			if err != nil {
				panic(err)
			}
			certPool.AppendCertsFromPEM(pem)
		}
	})
	var transport *http.Transport
	if certPool == nil {
		skipCertVerify := config.Bool(PlatformSkipVerifyCertKey)
		transport = &http.Transport{
			TLSClientConfig: &tls.Config{InsecureSkipVerify: skipCertVerify},
		}
	} else {
		transport = &http.Transport{
			TLSClientConfig: &tls.Config{RootCAs: certPool},
		}
	}
	client := &http.Client{
		Timeout:   time.Second * time.Duration(timeout),
		Transport: transport,
	}
	return &HttpClient{client: client, url: url}
}

func (c *HttpClient) Do(
	ctx context.Context,
	method string,
	url string,
	headers map[string]string,
	queryParams map[string]string,
	data any,
) (*http.Response, error) {
	var req *http.Request
	basicUrl := fmt.Sprintf("%s%s", c.url, url)
	err := validate(method, queryParams, data)
	if err != nil {
		FileLogger().Errorf(ctx, "Validation failed for the method: %s, err: %s", method, err.Error())
		return nil, err
	}
	//Add Query parameters to the request
	if queryParams != nil {
		first := true
		for k, v := range queryParams {
			if first {
				first = !first
				basicUrl = basicUrl + "?"
			} else {
				basicUrl = basicUrl + "&"
			}
			basicUrl = fmt.Sprintf("%s%s=%s", basicUrl, k, v)
		}
	}
	FileLogger().Infof(ctx, "Sending request to %s", basicUrl)
	if data == nil {
		req, err = http.NewRequest(method, basicUrl, nil)
	} else {
		var dataJson []byte
		dataJson, err = json.Marshal(data)
		if err != nil {
			return nil, err
		}
		req, err = http.NewRequest(method, basicUrl, bytes.NewBuffer(dataJson))
	}

	if err != nil {
		FileLogger().Errorf(
			ctx,
			"Error while creating new request url: %s, method: %s, err: %s",
			url,
			method,
			err.Error(),
		)
		return nil, err
	}

	if headers != nil {
		for k, v := range headers {
			req.Header.Set(k, v)
		}
	}

	res, err := c.client.Do(req)
	if err != nil {
		FileLogger().Errorf(ctx, "Http url: %s method: %s call failed %s", url, method, err.Error())
		return nil, err
	}

	FileLogger().Infof(ctx, "Http url: %s method: %s call succesful", url, method)

	return res, nil
}

// Validation to check if the given http method has correct
// signature.
func validate(method string, queryParams map[string]string, data any) error {
	if !isValidMethod(method) {
		return fmt.Errorf("Incorrect Method passed.")
	}

	if (method == http.MethodGet || method == http.MethodDelete) && data != nil {
		return fmt.Errorf("Incorrect request passed.")
	}

	return nil
}

// Validates the method passed.
func isValidMethod(method string) bool {
	switch method {
	case http.MethodGet,
		http.MethodPost,
		http.MethodDelete,
		http.MethodPut:
		return true
	}
	return false
}
