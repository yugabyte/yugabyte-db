//go:build testonly
// +build testonly

// Copyright (c) YugaByte, Inc.
package util

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"math/big"
	"net/http"
	"net/http/httptest"
	"node-agent/model"
	"os"
	"time"

	"github.com/gorilla/mux"
)

const (
	dummyPuuid        = "puuid"
	dummyInstanceType = "instance_type_0"
	dummyRegion       = "region_0"
	dummyZone         = "zone_0"
)

func init() {
	setUp()
}

func setUp() {
	// Sets the env to test to load test config.
	os.Setenv("env", "TEST")
	SetCurrentConfig("test-config")
	config := CurrentConfig()
	server := MockServer()
	config.Update(PlatformUrlKey, server.URL)
	config.Update(PlatformVersionKey, "1")
	config.Update(UserIdKey, "u1234")
	config.Update(ProviderIdKey, "p1234")
	config.Update(CustomerIdKey, "c1234")
	config.Update(NodeIpKey, "127.0.0.1")
	config.Update(RequestTimeoutKey, "100")
	config.Update(NodeNameKey, "nodeName")
	config.Update(NodeAgentIdKey, "n1234")
	config.Update(NodeRegionKey, dummyRegion)
	config.Update(NodeZoneKey, dummyZone)
	config.Update(NodeAzIdKey, "az1234")
	config.Update(NodeInstanceTypeKey, dummyInstanceType)
	config.Update(NodeLoggerKey, "node_agent_test.log")
	config.Update(PlatformCertsKey, "test")
	private, public := GetPublicAndPrivateKey()
	SaveCerts(
		context.TODO(),
		config,
		string(public),
		string(private),
		config.String(PlatformCertsKey),
	)
}

// Sets up a mock server to test http client calls.
func MockServer() *httptest.Server {
	r := mux.NewRouter()

	//Handle different routes for testing.
	r.HandleFunc("/api/v1/customers/{cuuid}/node_agents", registerNodeTestHandler)
	r.HandleFunc("/api/v1/customers/{cuuid}/node_agents/{nuuid}", nodeTestHandler)
	r.HandleFunc(
		"/api/customers/{cuuid}/providers/{puuid}/instance_types/{instanceType}",
		getInstanceTypeTestHandler,
	)
	r.HandleFunc("/test", testHandler)
	r.HandleFunc("/api/customers/{cuuid}/zones/{azid}/nodes", nodeCapabilitiesTestHandler)
	r.HandleFunc("/customers/{cuuid}/node_agents/{nuuid}/state", nodeAgentStateHandler)

	return httptest.NewServer(r)
}

// Todo: Create a mock request handler for state updates requests.
func nodeAgentStateHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodPut {
		//Todo
	} else {
		w.Write([]byte("success"))
	}
}

func getInstanceTypeTestHandler(w http.ResponseWriter, r *http.Request) {
	//vars := mux.Vars(r)
	data, err := json.Marshal(GetTestInstanceTypeData())
	if err != nil {
		http.Error(w, "Internal Server Error", 500)
		return
	}
	w.Write(data)
}

func testHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodGet {
		w.Write([]byte("success"))
	}

	if r.Method == http.MethodPut {
		http.Error(w, "Invalid request Method", 405)
	}
}
func registerNodeTestHandler(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	if vars["cuuid"] != "c1234" {
		http.Error(w, "{\"success\": false, \"error\": \"Bad Request\"}", 400)
		return
	}
	data, err := json.Marshal(GetTestRegisterResponse())
	if err != nil {
		http.Error(w, "Internal Server Error", 500)
		return
	}
	w.Write(data)
}

func nodeTestHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodDelete {
		http.Error(w, "Invalid request method.", 405)
		return
	}
	if r.Method == http.MethodDelete {
		res := model.ResponseMessage{}
		res.SuccessStatus = true
		res.Message = "Deleted node"
		data, err := json.Marshal(res)
		if err != nil {
			panic(err)
		}
		w.Write([]byte(data))
		return
	}
	w.Write([]byte("success"))
}

func nodeCapabilitiesTestHandler(w http.ResponseWriter, r *http.Request) {
	response := fmt.Sprintf(
		"{\"127.0.0.1\":{\"region\":\"%s\", \"instanceType\":\"%s\", \"zone\":\"%s\"}}",
		dummyRegion,
		dummyInstanceType,
		dummyZone,
	)
	w.Write([]byte(response))
}

func GetTestRegisterResponse() model.RegisterResponseSuccess {
	commonInfo := model.CommonInfo{
		Name:    "nodeName",
		IP:      "127.0.0.1",
		Version: "1",
	}
	config := model.NodeAgentConfig{
		ServerCert: "test_server_cert",
		ServerKey:  "test_server_key",
	}
	response := model.RegisterResponseSuccess{
		NodeAgent: model.NodeAgent{
			CommonInfo:   commonInfo,
			Uuid:         "n1234",
			UpdatedAt:    time.Now(),
			Config:       config,
			CustomerUuid: "c1234",
		},
	}
	return response
}

func GetTestProviderData() model.Provider {
	config := make(map[string]string)
	config["YB_HOME_DIR"] = "/home/yugabyte/custom"

	dummyProvider := model.Provider{
		BasicInfo: model.BasicInfo{Uuid: "12345"},
		SshPort:   54422,
		Config:    config,
	}
	return dummyProvider
}

func GetTestInstanceTypeData() model.NodeInstanceType {
	volumeDetails := model.VolumeDetails{VolumeSize: 100, MountPath: "/home"}
	nodeInstanceDetails := model.NodeInstanceTypeDetails{
		VolumeDetailsList: []model.VolumeDetails{volumeDetails},
	}
	result := model.NodeInstanceType{
		Active:           false,
		NumCores:         10,
		MemSizeGB:        10,
		Details:          nodeInstanceDetails,
		InstanceTypeCode: "instance_type_0",
		ProviderUuid:     GetTestProviderData().Uuid,
	}
	return result
}

func GetTestAccessKeyData(
	installNodeExporter bool,
	skipProvisioning bool,
	airGapInstall bool,
) model.AccessKey {
	result := model.AccessKey{
		KeyInfo: model.AccessKeyInfo{
			InstallNodeExporter: installNodeExporter,
			SkipProvisioning:    skipProvisioning,
			AirGapInstall:       airGapInstall,
		},
	}
	return result
}

func GetPublicAndPrivateKey() ([]byte, []byte) {
	ca := &x509.Certificate{
		SerialNumber: big.NewInt(2019),
		Subject: pkix.Name{
			Organization:  []string{"Yugabyte"},
			Country:       []string{"US"},
			Province:      []string{"CA"},
			Locality:      []string{"Sunnyvale"},
			StreetAddress: []string{"Yugabyte Street"},
			PostalCode:    []string{"94085"},
		},
		NotBefore: time.Now(),
		NotAfter:  time.Now().AddDate(10, 0, 0),
		IsCA:      true,
		ExtKeyUsage: []x509.ExtKeyUsage{
			x509.ExtKeyUsageClientAuth,
			x509.ExtKeyUsageServerAuth,
		},
		KeyUsage:              x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign,
		BasicConstraintsValid: true,
	}
	// Generate RSA key.
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		panic(err)
	}
	pub := key.Public()
	caBytes, err := x509.CreateCertificate(rand.Reader, ca, ca, pub, key)
	if err != nil {
		panic(err)
	}
	privateKey, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		panic(err)
	}
	// Encode private key to PEM.
	keyPEM := pem.EncodeToMemory(
		&pem.Block{
			Type:  "RSA PRIVATE KEY",
			Bytes: privateKey,
		},
	)
	// Encode public key to PEM.
	pubPEM := pem.EncodeToMemory(
		&pem.Block{
			Type:  "CERTIFICATE",
			Bytes: caBytes,
		},
	)
	return keyPEM, pubPEM
}
