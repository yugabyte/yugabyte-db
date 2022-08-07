/*
* Copyright (c) YugaByte, Inc.
*/

package main

import (
    "archive/tar"
    "bufio"
    "bytes"
    "compress/gzip"
    "crypto/rand"
    "encoding/base64"
    "fmt"
    "io"
    "io/ioutil"
    "log"
    "os"
    "os/exec"
    "strconv"
    "strings"
)

// Common constant for YUGAWARE_DUMP_FNAME in performing
var YUGAWARE_DUMP_FNAME string = "yugaware_dump.sql"

func ExecuteBashCommand(command string, args []string) (o string, e error) {

    cmd := exec.Command(command, args...)
    cmd.Stderr = os.Stderr
    out, err := cmd.Output()

    if err == nil {
        fmt.Println(command + " " + strings.Join(args, " ") + " successfully executed!")
    }

    return string(out), err
}

func IndexOf(arr []string, val string) int {

    for pos, v := range arr {
        if v == val {
            return pos
        }
    }

    return -1
}

func Contains(s []string, str string) bool {

    for _, v := range s {

        if v == str {
            return true
        }
    }
    return false
}

func YumInstall(args []string) {

    argsFull := append([]string{"-y", "install"}, args...)
    ExecuteBashCommand("yum", argsFull)
}

func FirewallCmdEnable(args []string) {

    argsFull := append([]string{"--zone=public", "--permanent"}, args...)
    ExecuteBashCommand("firewall-cmd", argsFull)
}

func TestSudoPermission() {

    cmd := exec.Command("id", "-u")
    output, err := cmd.Output()
    if err != nil {
        log.Fatal(err)
    }

    i, err := strconv.Atoi(string(output[:len(output)-1]))
    if err != nil {
        log.Fatal(err)
    }

    if i == 0 {
        fmt.Println("Awesome! You are now running this program with root permissions!")
    } else {
        log.Fatal("This program must be run as root! (sudo). Please try again with sudo.")
    }
}

func ExtractTarGz(gzipStream io.Reader) {

    uncompressedStream, err := gzip.NewReader(gzipStream)
    if err != nil {
        log.Fatal("ExtractTarGz: NewReader failed")
    }

    tarReader := tar.NewReader(uncompressedStream)
    for true {
        header, err := tarReader.Next()
        if err == io.EOF {
            break
        }
        if err != nil {
            log.Fatal("Skipping extraction: ExtractTarGz: Next() failed: %s", err.Error())
        }

        switch header.Typeflag {

        case tar.TypeDir:
            if err := os.Mkdir(header.Name, 0755); err != nil {
                fmt.Println("Skipping extraction: ExtractTarGz: Mkdir() failed: %s", err.Error())
            }

        case tar.TypeReg:

            outFile, err := os.Create(header.Name)
            if err != nil {
                log.Fatal("Skipping extraction: ExtractTarGz: Create() failed: %s", err.Error())
            }

            if _, err := io.Copy(outFile, tarReader); err != nil {
                log.Fatal("Skipping extraction: ExtractTarGz: Copy() failed: %s", err.Error())
            }

            outFile.Close()

        default:
            log.Fatal(
                "ExtractTarGz: uknown type: %s in %s",
                header.Typeflag,
                header.Name)
        }

    }
}


func ExtractTarGzYugabundle(gzipStream io.Reader) {

    uncompressedStream, err := gzip.NewReader(gzipStream)
    if err != nil {
        log.Fatal("ExtractTarGz: NewReader failed")
    }

    tarReader := tar.NewReader(uncompressedStream)

    for true {
        header, err := tarReader.Next()
        if err == io.EOF {
            break
        }
        if err != nil {
            log.Fatal("Skipping extraction: ExtractTarGz: Next() failed: %s", err.Error())
        }

        switch header.Typeflag {
        case tar.TypeDir:
            if err := os.Mkdir(header.Name, 0755); err != nil {
                fmt.Println("Skipping extraction: ExtractTarGz: Mkdir() failed: %s", err.Error())
            }

        case tar.TypeReg:

            directoryName := strings.Split(header.Name, "/")[0]
            fileName := strings.Split(header.Name, "/")[1]
            os.MkdirAll("/opt/yugabyte/packages/"+directoryName, os.ModePerm)
            os.Chdir("/opt/yugabyte/packages/" + directoryName)
            if _, err := os.Stat("/opt/yugabyte/packages/" + header.Name); err == nil {
                fmt.Println(header.Name + " already extracted, skipping.")
                break
            }

            outFile, err := os.Create(fileName)
            if err != nil {
                log.Fatal("Skipping extraction: ExtractTarGz: Create() failed: %s", err.Error())
            }

            if _, err := io.Copy(outFile, tarReader); err != nil {
                log.Fatal("Skipping extraction: ExtractTarGz: Copy() failed: %s", err.Error())
            }
            outFile.Close()

        default:
            log.Fatal(
                "ExtractTarGz: uknown type: %s in %s",
                header.Typeflag,
                header.Name)
        }

    }
}

func GenerateRandomBytes(n int) ([]byte, error) {

    b := make([]byte, n)
    _, err := rand.Read(b)
    if err != nil {
        return nil, err
    }
    return b, nil
}

func GenerateRandomStringURLSafe(n int) (string, error) {

    b, err := GenerateRandomBytes(n)
    return base64.URLEncoding.EncodeToString(b), err
}

func ReplaceTextGolang(fileName string, textToReplace string, textToReplaceWith string) {

    input, err := ioutil.ReadFile(fileName)
    if err != nil {
        fmt.Println(err)
        os.Exit(1)
    }

    output := bytes.Replace(input, []byte(textToReplace), []byte(textToReplaceWith), -1)
    if err = ioutil.WriteFile(fileName, output, 0666); err != nil {
        fmt.Println(err)
        os.Exit(1)

    }
}

func WriteTextIfNotExistsGolang(fileName string, textToWrite string) {

    byteFileService, errRead := ioutil.ReadFile(fileName)
    if errRead != nil {
        panic(errRead)
    }

    stringFileService := string(byteFileService)
    if !strings.Contains(stringFileService, textToWrite) {
        f, _ := os.OpenFile(fileName, os.O_APPEND|os.O_WRONLY, 0644)
        f.WriteString(textToWrite)
        f.Close()
    }
}

func CopyFileGolang(src string, dst string) {

    bytesRead, errSrc := ioutil.ReadFile(src)

    if errSrc != nil {
        log.Fatal(errSrc)
    }
    errDst := ioutil.WriteFile(dst, bytesRead, 0644)
    if errDst != nil {
        log.Fatal(errDst)
    }

    fmt.Println("Copy from " + src + " to " + dst + " executed successfully!")
}

func MoveFileGolang(src string, dst string) {

    err := os.Rename(src, dst)
    if err != nil {
        log.Fatal(err)
    }

    fmt.Println("Move from " + src + " to " + dst + " executed successfully!")
}

func GenerateCORSOrigin() string {

    command0 := "bash"
    args0 := []string{"-c", "ip route get 1.2.3.4 | awk '{print $7}'"}
    CORSOriginIP, _ := ExecuteBashCommand(command0, args0)
    CORSOrigin := "https://" + strings.TrimSuffix(CORSOriginIP, "\n") + ""
    return CORSOrigin
}

func WriteToWhitelist(command string, args []string) {

    _, err := ExecuteBashCommand(command, args)
    if err != nil {
        log.Println(err)
    } else {
        fmt.Println(args[1] + " executed!")
    }
}

func AddWhitelistRuleIfNotExists(rule string) {

    byteSudoers, errRead := ioutil.ReadFile("/etc/sudoers")
    if errRead != nil {
        fmt.Println(errRead)
    }

    stringSudoers := string(byteSudoers)

    if !strings.Contains(stringSudoers, rule) {
        command := "bash"
        argItem := "echo " + "'" + rule + "' | sudo EDITOR='tee -a' visudo"
        argList := []string{"-c", argItem}
        WriteToWhitelist(command, argList)
    }
}

func SetUpSudoWhiteList() {

    whitelistFile, err := os.Open("whitelistRules.txt")
    if err != nil {
        fmt.Println(err)
    }

    whitelistFileScanner := bufio.NewScanner(whitelistFile)
    whitelistFileScanner.Split(bufio.ScanLines)
    var whitelistRules []string
    for whitelistFileScanner.Scan() {
        whitelistRules = append(whitelistRules, whitelistFileScanner.Text())
    }

    whitelistFile.Close()
    for _, rule := range whitelistRules {
        AddWhitelistRuleIfNotExists(rule)
    }
}
