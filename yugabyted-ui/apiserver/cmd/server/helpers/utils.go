package helpers

import (
    "crypto/rand"
    "encoding/json"
    "errors"
    "fmt"
    "os"
    "io/ioutil"
    "math/big"
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
    executablePath, err := os.Executable()
    if err != nil {
        return "", fmt.Errorf("failed to get executable path: %s", err.Error())
    }
    executablePath, err = filepath.EvalSymlinks(executablePath)
    if err != nil {
        return "", fmt.Errorf("failed to evaluate symlink of %s: %s", executablePath, err.Error())
    }

    // Directory that is one level above directory containing yugabyted-ui binary
    yugabyteDir := filepath.Dir(filepath.Dir(executablePath))

    var dirCandidates []string
    if binaryName == "yb-controller-cli" {
        dirCandidates = []string{filepath.Join(yugabyteDir, "ybc", "bin")}
    } else {
        dirCandidates = []string{filepath.Join(yugabyteDir, "bin")}
    }
    for _, path := range dirCandidates {
       h.logger.Infof("Checking binary path %s in directory: %s",
                                       filepath.Join(path, binaryName), yugabyteDir)
       binaryPath, err := exec.LookPath(filepath.Join(path, binaryName))
       if err == nil {
           h.logger.Infof("Found binary %s at path: %s", binaryName, binaryPath)
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
// of the first successful request (status OK), or an error if all requests failed.
func (h *HelperContainer) AttemptGetRequests(
    urlList []string,
    expectJson bool,
) ([]byte, string, error) {
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    var body []byte
    successUrl := ""
    success := false
    h.logger.Debugf("getting requests from list of urls: %v", urlList)
    // Loop through all urls until one returns a response with no errors and status OK.
    // If expectJson is true, also fails the request if the response is not valid json.
    // If successful, should set err = nil and break from loop.
    // If failed, should set err != nil and continue.
    // At end of loop, check if err == nil to know if a request succeeded.
    for _, url := range urlList {
        resp, err := httpClient.Get(url)
        if err != nil {
            h.logger.Debugf("attempt to get request to %s failed with error: %s", url, err.Error())
            continue
        }
        if resp.StatusCode != http.StatusOK {
            resp.Body.Close()
            h.logger.Debugf("response from %s failed with status %s", url, resp.Status)
            continue
        }
        body, err = ioutil.ReadAll(resp.Body)
        resp.Body.Close()
        if err != nil {
            h.logger.Debugf("failed to read response from %s: %s", url, err.Error())
            continue
        }
        if !expectJson || json.Valid([]byte(body)) {
            h.logger.Debugf("good response from %s", url)
            successUrl = url
            success = true
            break
        }
        h.logger.Debugf("invalid json response from %s", url)
    }
    if !success {
        h.logger.Errorf("all requests to list of urls failed: %v", urlList)
        return nil, "", fmt.Errorf("all requests to list of urls failed: %v", urlList)
    }
    return body, successUrl, nil
}

// Attempts to append query parameteres to provided urlList. Modifies the original slice.
// Does not return error on failure, urls that fail to be parsed will be left unchanged.
func (h *HelperContainer) AppendQueryParams(urlList []string, params url.Values) {
    for index, baseUrl := range urlList {
        requestUrl, err := url.Parse(baseUrl)
        if err != nil {
            h.logger.Warnf("failed to parse url %s to add query params: %s",
                baseUrl, err.Error())
            continue
        }
        requestUrl.RawQuery = params.Encode()
        urlList[index] = requestUrl.String()
    }
}

// The purpose of this function is to create a list of urls to each master, for use with the
// AttemptGetRequest function. If the current node is a master, then the first entry of the list
// will use the current node's master address (i.e. we will prefer querying the current node
// first when calling AttemptGetRequest)
func (h *HelperContainer) BuildMasterURLs(path string, params url.Values) ([]string, error) {
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
            h.logger.Warnf("failed to construct url for %s:%s with path %s: %s",
                host, MasterUIPort, path, err.Error())
            continue
        }
        urlList = append(urlList, url)
    }
    // Add query params if provided
    if len(params) >= 1 {
        h.logger.Debugf("adding params %v", params)
        h.AppendQueryParams(urlList, params)
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
    go h.GetMastersFuture(mastersFuture)
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
            return
        }
        masterAddresses.HostList = h.ExtractMasterAddresses(mastersResponse)
    } else {
        masterAddresses.HostList = h.ExtractMasterAddressesList(mastersListResponse)
    }
    future <- masterAddresses
}

// BuildMasterURLsAndAttemptGetRequests first performs AttemptGetRequests using the cached
// master addresses. If they all fail, calls BuildMasterURLs, updates the cache, and tries
// AttemptGetRequests again.
func (h *HelperContainer) BuildMasterURLsAndAttemptGetRequests(
    path string,
    params url.Values,
    expectJson bool,
) ([]byte, error) {
    urlList := []string{}
    masterAddressCache := MasterAddressCache.Get()
    h.logger.Debugf("got cached master addresses %v", masterAddressCache)
    for _, host := range masterAddressCache {
        url, err := url.JoinPath(fmt.Sprintf("http://%s:%s", host, MasterUIPort), path)
        if err != nil {
            h.logger.Warnf("failed to construct url for %s:%s with path %s",
                host, MasterUIPort, path)
            continue
        }
        urlList = append(urlList, url)
    }
    if len(params) >= 1 {
        h.AppendQueryParams(urlList, params)
    }
    resp, successUrl, err := h.AttemptGetRequests(urlList, expectJson)
    if err == nil {
        // We will update the cache to move all failed addresses to the back of the slice
        // Parse host from successUrl
        parsedUrl, err := url.Parse(successUrl)
        if err != nil {
            h.logger.Warnf("couldn't parse host from url %s: %s", successUrl, err.Error())
        }
        successHost := parsedUrl.Hostname()
        for index, host := range masterAddressCache {
            if successHost == host {
                masterAddressCache =
                    append(masterAddressCache[index:], masterAddressCache[:index]...)
                break
            }
        }
        MasterAddressCache.Update(masterAddressCache)
        h.logger.Debugf("updated cached master addresses %v", masterAddressCache)
        return resp, err
    }
    h.logger.Warnf("get requests to cached master addresses %v failed: %s",
        urlList, err.Error())
    // BuildMasterURLs will try to get new master addresses and triggers cache refresh.
    urlList, err = h.BuildMasterURLs(path, params)
    if err != nil {
        h.logger.Errorf("failed to build master urls")
        return nil, err
    }
    body, _, err := h.AttemptGetRequests(urlList, expectJson)
    return body, err
}
