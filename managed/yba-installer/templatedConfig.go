/*
* Copyright (c) YugaByte, Inc.
*/

package main

import (
    "github.com/hashicorp/go-version"
    "bytes"
    "encoding/json"
    "fmt"
    "github.com/spf13/viper"
    "os"
    "path/filepath"
    "sigs.k8s.io/yaml"
    "strings"
    "text/template"
    "io/ioutil"
    "log"
    "github.com/xeipuuv/gojsonschema"
)

var availableData = map[string]string{}

// Validates that the parameters in each component's yml config file are indeed
// valid by turning the YAML file into a JSON file, and then validating that
// the parameters have been specified appropriately using the available
// JSON schema.
func ValidateJSONSchema(configYmlList []string) {

    for _, filename := range(configYmlList) {

        createdBytes, err := ioutil.ReadFile(filename)
        if err != nil {
        log.Fatalf("error: %v", err)
        }

            jsonString, jsonStringErr := yaml.YAMLToJSON(createdBytes)
        if jsonStringErr != nil {
            fmt.Printf("err: %v\n", jsonStringErr)
        }

        var jsonData map[string]interface{}
        if jsonDataError := json.Unmarshal([]byte(jsonString), &jsonData); jsonDataError != nil {
            fmt.Printf("err: %v\n", jsonDataError)
        }

        jsonBytesInput, _ := json.Marshal(jsonData)

        jsonStringInput := string(jsonBytesInput)

        serviceName := strings.TrimSuffix(strings.Split(filename, ".")[0], "\n")

        schemaLoader := gojsonschema.NewReferenceLoader("file://./"+serviceName+"-json-schema.json")
        documentLoader := gojsonschema.NewStringLoader(jsonStringInput)

        result, err := gojsonschema.Validate(schemaLoader, documentLoader)

        // Panic to automatically exit the Templating Phase if the passed-in parameters are
        // not valid.
        if err != nil {
            panic(err.Error())
        }

        if result.Valid() {
            fmt.Printf("The Ybanystaller configuration is valid!\n")
        } else {
            fmt.Printf("The Ybanystaller configuration is not valid! See Errors :\n")
            for _, desc := range result.Errors() {
                log.Fatalf("- %s\n", desc)
            }
        }

    }

}

// Reads info from input config file and sets all template parameters
// for each individual config file (for every component separately)
func ReadConfigAndSetVariables(configYmlList []string) {

    prometheusFileName := configYmlList[0]
    platformFileName := configYmlList[1]
    nginxFileName := configYmlList[2]

    viper.SetConfigName(prometheusFileName)
    viper.SetConfigType("yml")
    viper.AddConfigPath(".")
    err1 := viper.ReadInConfig()
    if err1 != nil {
        panic(err1)
    }

    prometheusOldConfig := viper.Get("prometheusOldConfig").(map[string]interface{})
    prometheusNewConfig := viper.Get("prometheusNewConfig").(map[string]interface{})
    prometheusOldService := viper.Get("prometheusOldService").(map[string]interface{})
    prometheusNewService := viper.Get("prometheusNewService").(map[string]interface{})

    availableData["PrometheusOldConfFileName"] = fmt.Sprint(prometheusOldConfig["filename"])
    availableData["PrometheusNewConfFileName"] = fmt.Sprint(prometheusNewConfig["filename"])
    availableData["PrometheusOldStoragePath"] = fmt.Sprint(prometheusOldService["storagepath"])
    availableData["PrometheusNewStoragePath"] = fmt.Sprint(prometheusNewService["storagepath"])

    viper.SetConfigName(platformFileName)
    viper.SetConfigType("yml")
    viper.AddConfigPath(".")
    err2 := viper.ReadInConfig()
    if err2 != nil {
        panic(err2)
    }

    platformOldConfig := viper.Get("platformOldConfig").(map[string]interface{})
    platformOldService := viper.Get("platformOldService").(map[string]interface{})
    platformNewService := viper.Get("platformNewService").(map[string]interface{})

    platformAppSecret, _ := GenerateRandomStringURLSafe(64)
    corsOrigin := GenerateCORSOrigin()
    platformAppSecretWritten := "\"" + strings.TrimSuffix(platformAppSecret, "\n") + "\""
    availableData["PlatformOldConfAppSecret"] = platformAppSecretWritten
    availableData["PlatformOldConfCorsOrigin"] = "\"" + strings.TrimSuffix(corsOrigin, "\n") + "\""

    availableData["PlatformOldConfDbUser"] = fmt.Sprint(platformOldConfig["platformdbuser"])
    availableData["PlatformOldConfDbPassword"] = fmt.Sprint(platformOldConfig["platformdbpassword"])

    // Will generate a random password for this field if not specified by the user.
    if availableData["PlatformOldConfDbPassword"] == "" {
        randomPassword, _ := GenerateRandomStringURLSafe(20)
        randomPasswordWritten := "\"" + strings.TrimSuffix(randomPassword, "\n") + "\""
        availableData["PlatformOldConfDbPassword"] = randomPasswordWritten
    }

    oldDevopsHome := fmt.Sprint(platformOldConfig["devopshome"])
    writtenDevops := "\"" + strings.TrimSuffix(oldDevopsHome, "\n") + "\""

    availableData["PlatformOldConfDevopsHome"] = writtenDevops

    oldSwamperPath := fmt.Sprint(platformOldConfig["swampertargetpath"])
    writtenSwamperPath := "\"" + strings.TrimSuffix(oldSwamperPath, "\n") + "\""

    availableData["PlatformOldConfSwamperTargetPath"] = writtenSwamperPath

    oldMetricsUrl := fmt.Sprint(platformOldConfig["metricsurl"])
    writtenMetricsUrl := "\"" + strings.TrimSuffix(oldMetricsUrl, "\n") + "\""

    availableData["PlatformOldConfMetricsUrl"] = writtenMetricsUrl

    oldUseOauth := fmt.Sprint(platformOldConfig["useoauth"])
    availableData["PlatformOldConfUseOauth"] = oldUseOauth

    oldYbSecurityType := fmt.Sprint(platformOldConfig["ybsecuritytype"])
    writtenYbSecurityType := "\"" + strings.TrimSuffix(oldYbSecurityType, "\n") + "\""

    availableData["PlatformOldConfYbSecurityType"] = writtenYbSecurityType

    oldYbOidcClientId := fmt.Sprint(platformOldConfig["yboidcclientid"])
    writtenYbOidcClientId := "\"" + strings.TrimSuffix(oldYbOidcClientId, "\n") + "\""
    availableData["PlatformOldConfYbOidcClientId"] = writtenYbOidcClientId

    oldYbOidcSecret := fmt.Sprint(platformOldConfig["yboidcsecret"])
    writtenYbOidcSecret := "\"" + strings.TrimSuffix(oldYbOidcSecret, "\n") + "\""
    availableData["PlatformOldConfYbOidcSecret"] = writtenYbOidcSecret

    oldDiscoveryUri := fmt.Sprint(platformOldConfig["yboidcdiscoveryuri"])
    writtenDiscoveryUri := "\"" + strings.TrimSuffix(oldDiscoveryUri, "\n") + "\""
    availableData["PlatformOldConfYbOidcDiscoveryUri"] = writtenDiscoveryUri

    oldYwUrl := fmt.Sprint(platformOldConfig["ywurl"])
    writtenYwUrl := "\"" + strings.TrimSuffix(oldYwUrl, "\n") + "\""
    availableData["PlatformOldConfYwUrl"] = writtenYwUrl

    oldYbOidcScope := fmt.Sprint(platformOldConfig["yboidcscope"])
    writtenYbOidcScope := "\"" + strings.TrimSuffix(oldYbOidcScope, "\n") + "\""
    availableData["PlatformOldConfYbOidcScope"] = writtenYbOidcScope

    oldYbOidcEmailAttr := fmt.Sprint(platformOldConfig["yboidcemailattr"])
    writtenYbOidcEmailAttr := "\"" + strings.TrimSuffix(oldYbOidcEmailAttr, "\n") + "\""
    availableData["PlatformOldConfYbOidcEmailAttr"] = writtenYbOidcEmailAttr

    availableData["PlatformOldServiceAppSecret"] = strings.TrimSuffix(platformAppSecret, "\n")
    availableData["PlatformOldServiceCorsOrigin"] = strings.TrimSuffix(corsOrigin, "\n")

    oldServiceAuth := fmt.Sprint(platformOldService["useoauth"])
    availableData["PlatformOldServiceUseOauth"] = oldServiceAuth

    oldServiceSecurityType := fmt.Sprint(platformOldService["ybsecuritytype"])
    availableData["PlatformOldServiceYbSecurityType"] = oldServiceSecurityType

    oldServiceOidcClientId := fmt.Sprint(platformOldService["yboidcclientid"])
    availableData["PlatformOldServiceYbOidcClientId"] = oldServiceOidcClientId

    oldServiceOidcSecret := fmt.Sprint(platformOldService["yboidcsecret"])
    availableData["PlatformOldServiceYbOidcSecret"] = oldServiceOidcSecret

    oldServiceOidcDiscoveryUri := fmt.Sprint(platformOldService["yboidcdiscoveryuri"])
    availableData["PlatformOldServiceYbOidcDiscoveryUri"] = oldServiceOidcDiscoveryUri

    availableData["PlatformOldServiceYwUrl"] = fmt.Sprint(platformOldService["ywurl"])
    availableData["PlatformOldServiceYbOidcScope"] = fmt.Sprint(platformOldService["yboidcscope"])

    oldServiceOidcEmailAttr := fmt.Sprint(platformOldService["yboidcemailattr"])
    availableData["PlatformOldServiceYbOidcEmailAttr"] = oldServiceOidcEmailAttr

    availableData["PlatformNewServiceAppSecret"] = strings.TrimSuffix(platformAppSecret, "\n")
    availableData["PlatformNewServiceCorsOrigin"] = strings.TrimSuffix(corsOrigin, "\n")

    newServiceAuth := fmt.Sprint(platformNewService["useoauth"])
    availableData["PlatformNewServiceUseOauth"] = newServiceAuth

    newServiceSecurityType := fmt.Sprint(platformNewService["ybsecuritytype"])
    availableData["PlatformNewServiceYbSecurityType"] = newServiceSecurityType

    newServiceOidcClientId := fmt.Sprint(platformNewService["yboidcclientid"])
    availableData["PlatformNewServiceYbOidcClientId"] = newServiceOidcClientId

    newServiceOidcSecret := fmt.Sprint(platformNewService["yboidcsecret"])
    availableData["PlatformNewServiceYbOidcSecret"] = newServiceOidcSecret

    newServiceOidcDiscoveryUri := fmt.Sprint(platformNewService["yboidcdiscoveryuri"])
    availableData["PlatformNewServiceYbOidcDiscoveryUri"] = newServiceOidcDiscoveryUri

    availableData["PlatformNewServiceYwUrl"] = fmt.Sprint(platformNewService["ywurl"])
    availableData["PlatformNewServiceYbOidcScope"] = fmt.Sprint(platformNewService["yboidcscope"])

    newServiceOidcEmailAttr := fmt.Sprint(platformNewService["yboidcemailattr"])
    availableData["PlatformNewServiceYbOidcEmailAttr"] = newServiceOidcEmailAttr

    viper.SetConfigName(nginxFileName)
    viper.SetConfigType("yml")
    viper.AddConfigPath(".")
    err3 := viper.ReadInConfig()
    if err3 != nil {
        panic(err3)
    }

    nginxHttp := viper.Get("nginxHttp").(map[string]interface{})
    nginxHttps := viper.Get("nginxHttps").(map[string]interface{})

    availableData["NginxHttpServerName"] = fmt.Sprint(nginxHttp["servername"])
    availableData["NginxHttpsServerName"] = fmt.Sprint(nginxHttps["servername"])

}

func Configure(filePath string) ([]byte, error) {

    tmpl, err := template.New(filepath.Base(filePath)).ParseFiles(filePath)
    if err != nil {
        return nil, err
    }

    var buf bytes.Buffer
    if err := tmpl.Execute(&buf, availableData); err != nil {
        return nil, err
    }

    return buf.Bytes(), nil

    }

    func ReadYAMLtoJSON(createdBytes []byte) (map[string]interface{}, error) {

    jsonString, jsonStringErr := yaml.YAMLToJSON(createdBytes)
    if jsonStringErr != nil {
        fmt.Printf("err: %v\n", jsonStringErr)
        return nil, jsonStringErr
    }

    var jsonData map[string]interface{}
    if jsonDataError := json.Unmarshal([]byte(jsonString), &jsonData); jsonDataError != nil {
        fmt.Printf("err: %v\n", jsonDataError)
        return nil, jsonDataError
    }

    return jsonData, nil

}

func WriteBytes(byteSlice []byte, fileName []byte) ([]byte, error) {

    fileNameString := string(fileName)

    file, createErr := os.OpenFile(
        fileNameString,
        os.O_WRONLY|os.O_TRUNC|os.O_CREATE,
        os.ModePerm,
    )

    if createErr != nil {
        return nil, createErr
    }

    defer file.Close()
    _, writeErr := file.Write(byteSlice)
    if writeErr != nil {
        return nil, writeErr
    }

    return []byte("Wrote bytes to " + fileNameString + " successfully!"), nil

}

func GenerateTemplatedConfiguration(vers string, httpMode string) {

    configYmlList := []string{"yba-installer-input-prometheus.yml",
    "yba-installer-input-platform.yml", "yba-installer-input-nginx.yml"}

    ValidateJSONSchema(configYmlList)

    ReadConfigAndSetVariables(configYmlList)

    outputYmlList :=  []string{"yba-installer-nginx.yml",
    "yba-installer-prometheus.yml",
    "yba-installer-platform.yml"}

    for _, outYmlName := range(outputYmlList) {

        createdBytes, _ := Configure(outYmlName)
        jsonData, _ := ReadYAMLtoJSON(createdBytes)

        numberOfServices := len(jsonData["services"].([]interface{}))

        v1, _ := version.NewVersion(vers)
        v2, _ := version.NewVersion("2.8.0.0")
        isOld := v1.LessThan(v2)

        for i := 0; i < numberOfServices; i++ {

        service := jsonData["services"].([]interface{})[i]
        serviceName := fmt.Sprint(service.(map[string]interface{})["name"])

        serviceFileName := fmt.Sprint(service.(map[string]interface{})["fileName"])
        serviceContents := fmt.Sprint(service.(map[string]interface{})["contents"])


            if strings.Contains(serviceName, "nginx") {
                if httpMode == "http" && serviceName == "nginxHttp" {
                    WriteBytes([]byte(serviceContents), []byte(serviceFileName))
                    fmt.Println("Templated configuration for " + serviceName +
                    " succesfully applied!")
                } else if httpMode == "https" && serviceName == "nginxHttps" {
                    WriteBytes([]byte(serviceContents), []byte(serviceFileName))
                    fmt.Println("Templated configuration for " + serviceName +
                    " succesfully applied!")
            }
        }

            if isOld {
                if strings.Contains(serviceName, "Old") {
                    WriteBytes([]byte(serviceContents), []byte(serviceFileName))
                    fmt.Println("Templated configuration for " + serviceName +
                    " succesfully applied!")
                }

                } else {
                if strings.Contains(serviceName, "New") {
                    WriteBytes([]byte(serviceContents), []byte(serviceFileName))
                    fmt.Println("Templated configuration for " + serviceName +
                    " succesfully applied!")
                }
            }
    }

    }
}
