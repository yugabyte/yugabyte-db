package helpers
import (
    "crypto/rand"
    "math/big"
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
