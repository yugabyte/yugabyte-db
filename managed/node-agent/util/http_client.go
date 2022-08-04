// Copyright (c) YugaByte, Inc.
package util

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

type HttpClient struct {
	client *http.Client
	host   string
	port   string
}

func NewHttpClient(timeout int, host string, port string) *HttpClient {
	client := &http.Client{
		Timeout: time.Second * time.Duration(timeout),
	}
	return &HttpClient{client: client, host: host, port: port}
}

func (c *HttpClient) Do(
	method string,
	url string,
	headers map[string]string,
	queryParams map[string]string,
	data any,
) (*http.Response, error) {
	var (
		req *http.Request
		err error
	)
	basicUrl := fmt.Sprintf("%s:%s%s", c.host, c.port, url)
	err = validate(method, queryParams, data)
	if err != nil {
		FileLogger.Errorf("Validation failed for the method: %s, err: %s", method, err.Error())
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

	if data == nil {
		req, err = http.NewRequest(method, basicUrl, nil)
	} else {
		dataJson, err := json.Marshal(data)
		if err != nil {
			return nil, err
		}
		req, err = http.NewRequest(method, basicUrl, bytes.NewBuffer(dataJson))
	}

	if err != nil {
		FileLogger.Errorf(
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
		FileLogger.Errorf("Http url: %s method: %s call failed %s", url, method, err.Error())
		return nil, err
	}

	FileLogger.Infof("Http url: %s method: %s call succesful", url, method)

	return res, nil
}

//Validation to check if the given http method has correct
//signature.
func validate(method string, queryParams map[string]string, data any) error {
	if !isValidMethod(method) {
		return fmt.Errorf("Incorrect Method passed.")
	}

	if (method == http.MethodGet || method == http.MethodDelete) && data != nil {
		return fmt.Errorf("Incorrect request passed.")
	}

	return nil
}

//Validates the method passed.
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
