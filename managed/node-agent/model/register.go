// Copyright (c) YugabyteDB, Inc.

package model

import (
	"fmt"
	"time"
)

type RegisterRequest struct {
	CommonInfo
}

type NodeAgent struct {
	CommonInfo
	Uuid         string          `json:"uuid,omitempty"`
	CustomerUuid string          `json:"customerUuid,omitempty"`
	UpdatedAt    time.Time       `json:"updatedAt,omitempty"`
	Config       NodeAgentConfig `json:"config,omitempty"`
}

type RegisterResponseSuccess struct {
	NodeAgent
}

type ResponseError struct {
	SuccessStatus bool   `json:"success,omitempty"`
	Message       string `json:"error,omitempty"`
}

type ResponseMessage struct {
	SuccessStatus bool   `json:"success,omitempty"`
	Message       string `json:"message,omitempty"`
}

type CommonInfo struct {
	Name     string `json:"name,omitempty"`
	IP       string `json:"ip,omitempty"`
	State    string `json:"state,omitempty"`
	Version  string `json:"version,omitempty"`
	ArchType string `json:"archType,omitempty"`
	OSType   string `json:"osType,omitempty"`
	Home     string `json:"home,omitempty"`
	Port     int    `json:"port,omitempty"`
}

type NodeAgentConfig struct {
	ServerCert string `json:"serverCert,omitempty"`
	ServerKey  string `json:"serverKey,omitempty"`
}

type Customer struct {
	CustomerId   string `json:"uuid,omitempty"`
	CustomerName string `json:"name,omitempty"`
	CustomerCode string `json:"code,omitempty"`
}

type User struct {
	UserId     string `json:"uuid,omitempty"`
	CustomerId string `json:"customerUUID,omitempty"`
	Email      string `json:"email,omitempty"`
	Role       string `json:"role,omitempty"`
}

type SessionInfo struct {
	CustomerId string `json:"customerUUID,omitempty"`
	UserId     string `json:"UserUUID,omitempty"`
}

type DisplayInterface interface {
	Id() string
	String() string
	Name() string
}

// Id implements the method in DisplayInterface.
func (c Customer) Id() string {
	return c.CustomerId
}

// String implements the method in DisplayInterface.
func (c Customer) String() string {
	return fmt.Sprintf("Customer ID: %s, Customer Name: %s", c.CustomerId, c.CustomerCode)
}

// Name implements the method in DisplayInterface.
func (c Customer) Name() string {
	return c.CustomerName
}

// Id implements the method in DisplayInterface.
func (u User) Id() string {
	return u.UserId
}

// String implements the method in DisplayInterface.
func (u User) String() string {
	return fmt.Sprintf("User ID: %s, Role: %s", u.UserId, u.Role)
}

// Name implements the method in DisplayInterface.
func (u User) Name() string {
	return u.Email
}

func (err *ResponseError) Error() string {
	return err.Message
}
