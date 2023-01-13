package helpers
import (
    "crypto/rand"
    "errors"
    "fmt"
    "math/big"
    "os/exec"
    "path/filepath"
    "strconv"
    "strings"
)

func Random128BitString() (string, error) {
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
func GetIntVersion(versionNumber string) []int64 {
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
func CompareVersions(versionA string, versionB string) int64 {
    intVersionA := GetIntVersion(versionA)
    intVersionB := GetIntVersion(versionB)
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
func GetSmallestVersion(versionInfoFutures []chan VersionInfoFuture) string {
    smallestVersion := ""
    for _, versionInfoFuture := range versionInfoFutures {
            versionInfo := <-versionInfoFuture
            if versionInfo.Error == nil {
                    versionNumber := versionInfo.VersionInfo.VersionNumber
                    if smallestVersion == "" ||
                            CompareVersions(smallestVersion, versionNumber) > 0 {
                            smallestVersion = versionNumber
                    }
            }
    }
    return smallestVersion
}

func GetBytesFromString(sizeString string) (int64, error) {
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

func FindBinaryLocation(binaryName string) (string, error) {
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
