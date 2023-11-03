// Copyright (c) YugaByte, Inc.

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
	Uuid         string          `json:"uuid"`
	CustomerUuid string          `json:"customerUuid"`
	UpdatedAt    time.Time       `json:"updatedAt"`
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
	Name     string `json:"name"`
	IP       string `json:"ip"`
	State    string `json:"state"`
	Version  string `json:"version"`
	ArchType string `json:"archType"`
	OSType   string `json:"osType"`
	Home     string `json:"home"`
	Port     int    `json:"port"`
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
	Id() string
	String() string
}

// Id implements the method in DisplayInterface.
func (c Customer) Id() string {
	return c.CustomerId
}

// String implements the method in DisplayInterface.
func (c Customer) String() string {
	return fmt.Sprintf("Customer ID: %s, Customer Name: %s", c.CustomerId, c.CustomerCode)
}

// Id implements the method in DisplayInterface.
func (u User) Id() string {
	return u.UserId
}

// String implements the method in DisplayInterface.
func (u User) String() string {
	return fmt.Sprintf("User ID: %s, Role: %s", u.UserId, u.Role)
}

func (err *ResponseError) Error() string {
	return err.Message
}
