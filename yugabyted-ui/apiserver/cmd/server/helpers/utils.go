package helpers

import (
    "crypto/rand"
    "encoding/json"
    "errors"
    "fmt"
    "io/ioutil"
    "math/big"
    "net"
    "net/http"
    "net/url"
    "os/exec"
    "path/filepath"
    "strconv"
    "strings"
    "time"
)

func (h *HelperContainer) Random128BitString() (string, error) {
    //Max random value, a 128-bits integer, i.e 2^128 - 1
    max := new(big.Int)
    max.Exp(big.NewInt(2), big.NewInt(128), nil).Sub(max, big.NewInt(1))

    //Generate cryptographically strong pseudo-random between 0 - max
    n, err := rand.Int(rand.Reader, max)
    if err != nil {
        return "", err
    }

    //String representation of n in hex
    nonce := n.Text(16)

    return nonce, nil
}

// Convert a version number string into a slice of integers. Will only get the major, minor, and
// patch numbers
func (h *HelperContainer) GetIntVersion(versionNumber string) []int64 {
    versions := strings.Split(versionNumber, ".")
    intVersions := make([]int64, 3)
    for i, version := range versions {
        if i > 2 {
            break
        }
        intVersion, err := strconv.ParseInt(version, 10, 64)
        if err == nil {
            intVersions[i] = intVersion
        }
    }
    return intVersions
}

// Compares two version strings. Will only compare major, minor, patch numbers.
// - If versionA == versionB, return 0
// - If versionA < versionB, return -1
// - If versionA > versionB, return 1
func (h *HelperContainer) CompareVersions(versionA string, versionB string) int64 {
    intVersionA := h.GetIntVersion(versionA)
    intVersionB := h.GetIntVersion(versionB)
    for i := 0; i < 3; i++ {
        if intVersionA[i] < intVersionB[i] {
            return -1
        } else if intVersionA[i] > intVersionB[i] {
            return 1
        }
    }
    return 0
}

// Gets the smallest version
func (h *HelperContainer) GetSmallestVersion(
    versionInfoFutures map[string]chan VersionInfoFuture,
) string {
    smallestVersion := ""
    for host, versionInfoFuture := range versionInfoFutures {
            versionInfo := <-versionInfoFuture
            if versionInfo.Error == nil {
                    versionNumber := versionInfo.VersionInfo.VersionNumber
                    if smallestVersion == "" ||
                            h.CompareVersions(smallestVersion, versionNumber) > 0 {
                            smallestVersion = versionNumber
                    }
            } else {
                h.logger.Warnf("failed to get version from %s", host)
            }
    }
    return smallestVersion
}

func (h *HelperContainer) GetBytesFromString(sizeString string) (int64, error) {
    // Possible units are BKMGTPE, for byte, kilobyte, megabyte, etc.
    if len(sizeString) < 1 {
        return 0, nil
    }
    unit := string([]rune(sizeString)[len(sizeString) - 1])
    valString := string([]rune(sizeString)[0:len(sizeString)-1])
    conversionFactor := int64(0)
    switch unit {
    case "B":
        conversionFactor = 1
    case "K":
        conversionFactor = 1024
    case "M":
        conversionFactor = 1024 * 1024
    case "G":
        conversionFactor = 1024 * 1024 * 1024
    case "T":
        conversionFactor = 1024 * 1024 * 1024 * 1024
    case "P":
        conversionFactor = 1024 * 1024 * 1024 * 1024 * 1024
    case "E":
        conversionFactor = 1024 * 1024 * 1024 * 1024 * 1024 * 1024
    default:
        return 0, errors.New("could not find unit for table size")
    }
    val, err := strconv.ParseFloat(valString, 64)
    if err != nil {
        return 0, err
    }
    byteVal := int64(val * float64(conversionFactor))
    return byteVal, nil
}

func (h *HelperContainer) FindBinaryLocation(binaryName string) (string, error) {
    YUGABYTE_DIR := filepath.Join("..", "..")

    dirCandidates := []string{
        // Default if tar is downloaded
        filepath.Join(YUGABYTE_DIR, "bin"),
        // Development environment
        filepath.Join(YUGABYTE_DIR, "build", "latest", "bin"),
        // Development environment for UI
        filepath.Join(YUGABYTE_DIR, "build", "latest", "gobin"),
    }
    for _, path := range dirCandidates {
        binaryPath, err := exec.LookPath(filepath.Join(path, binaryName))
        if err == nil {
            return binaryPath, err
        }
    }
    return "", fmt.Errorf("failed to find binary %s", binaryName)
}

// Removes all 127 addresses from a list of addresses, unless every address is a 127 address,
// in which case it keeps one
func (h *HelperContainer) RemoveLocalAddresses(nodeList []string) []string {
    listToReturn := []string{}
    nonLocalAddressFound := false
    for _, address := range nodeList {
        if len(listToReturn) > 0 && len(address) >= 3 && address[0:3] == "127" {
            // Don't add 127 address to list if list is non-empty
            continue
        }
        if !nonLocalAddressFound && (len(address) < 3 || address[0:3] != "127") {
            // The first time we encounter a non 127 address, clear the list
            nonLocalAddressFound = true
            listToReturn = nil
        }
        listToReturn = append(listToReturn, address)
    }
    return listToReturn
}

// Attempt GET requests to every URL in the provided slice, one at a time. Returns the result
// of the first successful request (status OK), or the most recent error if all requests failed.
func (h *HelperContainer) AttemptGetRequests(urlList []string, expectJson bool) ([]byte, error) {
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    var resp *http.Response
    var err error
    var body []byte
    h.logger.Debugf("getting requests from list of urls: %v", urlList)
    for _, url := range urlList {
        resp, err = httpClient.Get(url)
        if err != nil {
            h.logger.Debugf("attempt to get request to %s failed with error: %s", url, err.Error())
            continue
        }
        if resp.StatusCode != http.StatusOK {
            resp.Body.Close()
            h.logger.Debugf("response from %s failed with status %s", url, resp.Status)
        } else {
            body, err = ioutil.ReadAll(resp.Body)
            resp.Body.Close()
            if err != nil {
                h.logger.Debugf("failed to read response from %s: %s", url, err.Error())
                err = fmt.Errorf("failed to read response: %s", err.Error())
            } else if !expectJson || json.Valid([]byte(body)) {
                h.logger.Debugf("good response from %s", url)
                err = nil
                break
            }
        }
    }
    if err != nil {
        h.logger.Errorf("all requests to list of urls failed: %v", urlList)
        return nil, errors.New("all requests to list of urls failed")
    }
    return body, nil
}

// The purpose of this function is to create a list of urls to each master, for use with the
// AttemptGetRequest function. If the current node is a master, then the first entry of the list
// will use the current node's master address (i.e. we will prefer querying the current node
// first when calling AttemptGetRequest)
func (h *HelperContainer) BuildMasterURLs(path string) ([]string, error) {
    urlList := []string{}
    masterAddressesFuture := make(chan MasterAddressesFuture)
    go h.GetMasterAddressesFuture(masterAddressesFuture)
    masterAddressesResponse := <-masterAddressesFuture
    if masterAddressesResponse.Error != nil {
        h.logger.Errorf("failed to get master addresses")
        return urlList, masterAddressesResponse.Error
    }
    for _, host := range masterAddressesResponse.HostList {
        url, err := url.JoinPath(fmt.Sprintf("http://%s:%s", host, MasterUIPort), path)
        if err != nil {
            h.logger.Warnf("failed to construct url for %s:%s with path %s",
                host, MasterUIPort, path)
            continue
        }
        urlList = append(urlList, url)
    }
    return urlList, nil
}

type MasterAddressesFuture struct {
    HostList []string
    Error    error
}

func (h *HelperContainer) GetMasterAddressesFuture(future chan MasterAddressesFuture) {
    // Attempt to get a list of masters from both master and tserver, in case one of them is down
    masterAddresses := MasterAddressesFuture{
        HostList: []string{},
        Error:    nil,
    }
    mastersFuture := make(chan MastersFuture)
    go h.GetMastersFuture(HOST, mastersFuture)
    mastersListFuture := make(chan MastersListFuture)
    go h.GetMastersFromTserverFuture(HOST, mastersListFuture)
    mastersListResponse := <-mastersListFuture
    if mastersListResponse.Error != nil {
        h.logger.Debugf("failed to get masters list from tserver at %s: %s",
            HOST, mastersListResponse.Error.Error())
        mastersResponse := <-mastersFuture
        if mastersResponse.Error != nil {
            h.logger.Errorf("failed to get masters from master and tserver at %s: %s",
                HOST, mastersResponse.Error.Error())
            masterAddresses.Error =
                fmt.Errorf("failed to get masters from master and tserver at %s: %s",
                    HOST, mastersResponse.Error.Error())
            future <- masterAddresses
        }

        for _, master := range mastersResponse.Masters {
            if len(master.Registration.PrivateRpcAddresses) > 0 {
                host := master.Registration.PrivateRpcAddresses[0].Host
                if host == HOST {
                    masterAddresses.HostList = append([]string{host}, masterAddresses.HostList...)
                } else {
                    masterAddresses.HostList = append(masterAddresses.HostList, host)
                }
            }
        }
    } else {
        for _, master := range mastersListResponse.Masters {
            host, _, err := net.SplitHostPort(master.HostPort)
            if err != nil {
                h.logger.Warnf("failed to split host and port of %s", master.HostPort)
                continue
            }
            if host == HOST {
                masterAddresses.HostList = append([]string{host}, masterAddresses.HostList...)
            } else {
                masterAddresses.HostList = append(masterAddresses.HostList, host)
            }
        }
    }
    future <- masterAddresses
}
