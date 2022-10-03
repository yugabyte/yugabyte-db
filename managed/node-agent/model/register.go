// Copyright (c) YugaByte, Inc.

package model

import (
	"fmt"
)

type RegisterRequest struct {
	CommonInfo
}

type NodeAgent struct {
	CommonInfo
	Uuid         string          `json:"uuid"`
	CustomerUuid string          `json:"customerUuid"`
	UpdatedAt    int             `json:"updatedAt"`
	Config       NodeAgentConfig `json:"config"`
}

type RegisterResponseSuccess struct {
	NodeAgent
}

type ResponseError struct {
	SuccessStatus bool   `json:"success"`
	Message       string `json:"error"`
}

type ResponseMessage struct {
	SuccessStatus bool   `json:"success"`
	Message       string `json:"message"`
}

type CommonInfo struct {
	Name    string `json:"name"`
	IP      string `json:"ip"`
	State   string `json:"state"`
	Version string `json:"version"`
}

type NodeAgentConfig struct {
	ServerCert string `json:"serverCert"`
	ServerKey  string `json:"serverKey"`
}

type Customer struct {
	CustomerId   string `json:"uuid"`
	CustomerName string `json:"name"`
	CustomerCode string `json:"code"`
}

type User struct {
	UserId     string `json:"uuid"`
	CustomerId string `json:"customerUUID"`
	Role       string `json:"role"`
}

type SessionInfo struct {
	CustomerId string `json:"customerUUID"`
	UserId     string `json:"UserUUID"`
}

type DisplayInterface interface {
	ToString() string
}

func (c Customer) ToString() string {
	return fmt.Sprintf("Customer ID: %s, Customer Name: %s", c.CustomerId, c.CustomerCode)
}

func (u User) ToString() string {
	return fmt.Sprintf("User ID: %s, Role: %s", u.UserId, u.Role)
}

func (err *ResponseError) Error() string {
	return err.Message
}
