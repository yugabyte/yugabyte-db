package license

import (
	"crypto/sha256"
	"encoding/base64"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/licensing/pubkey"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// ErrorInvalidLicenseFormat for incorrectly formatted license files
var ErrorInvalidLicenseFormat error = errors.New("invalid license format")

// License is exposes basic functionality needed by license files.
type License struct {
	Raw         string
	EncodedData string
	Signature   []byte
}

// FromFile will create a license from the given license file.
func FromFile(filePath string) (*License, error) {
	var lic *License = &License{}
	// If the path is not absolute, covert it to an absolute filepath
	filePath, err := filepath.Abs(filePath)
	if err != nil {
		return lic, fmt.Errorf("failed parsing license filepath: %w", err)
	}
	log.Debug("installing license from " + filePath)
	if _, err := os.Stat(filePath); errors.Is(err, fs.ErrNotExist) {
		return lic, err
	}

	data, err := os.ReadFile(filePath)
	if err != nil {
		return lic, err
	}
	lic.Raw = string(data)
	return lic, lic.ParseRaw()
}

// FromInstalledLicense gets the installed license, if one exists.
// An empty license will be returned if one is not installed, allowing validation
// to still be performed. Mainly, validation can pass if a public key has not been
// embedded at build time (meaning, we are using a dev build)
func FromInstalledLicense() (*License, error) {
	// The installed license path doesn't exist, return an empty license.
	if _, err := os.Stat(common.LicenseFile()); errors.Is(err, fs.ErrNotExist) {
		return nil, err
	}
	return FromFile(common.LicenseFile())
}

// FromParts creates a license from the individual parts of a license
func FromParts(data string, sig []byte) *License {
	shaSig := base64.StdEncoding.EncodeToString(sig)
	raw := fmt.Sprintf("%s:%s", data, shaSig)
	return &License{
		Raw:         raw,
		EncodedData: data,
		Signature:   sig,
	}
}

// ParseRaw will take the raw data of the license and split it into the encoded data and the
// signature.
func (l *License) ParseRaw() error {
	// Nothing to parse.
	if len(l.Raw) == 0 {
		return nil
	}
	split := strings.Split(l.Raw, ":")
	if len(split) != 2 {
		return ErrorInvalidLicenseFormat
	}
	l.EncodedData = split[0]
	sig, err := base64.StdEncoding.DecodeString(split[1])
	l.Signature = sig
	return err
}

// WriteToLocation will copy the license file to the given path. Used to provide the license to yba.
// newLocation will be the full path, including the file name, to where we will install the license.
func (l License) WriteToLocation(newLocation string) error {
	if _, err := os.Stat(newLocation); !errors.Is(err, fs.ErrNotExist) {
		if err == nil {
			log.Debug("Found existing license... overwritting it")
		} else {
			return err
		}
	}

	dstFile, err := os.Create(newLocation)
	if err != nil {
		return err
	}
	defer dstFile.Close()

	_, err = dstFile.Write([]byte(l.Raw))
	return err
}

// Sha256Data gets the sha256 of our encoded data
func (l License) Sha256Data() []byte {
	data := sha256.Sum256([]byte(l.EncodedData))
	return data[:]
}

// Validate will check if the given license is actually valid.
func (l License) Validate() bool {
	sha256Data := l.Sha256Data()
	sha256String := string(sha256Data)
	for _, bad := range revokedLicenses() {
		if bad == sha256String {
			fmt.Println("license has been blacklisted")
			return false
		}
	}

	return pubkey.Validate(sha256Data, l.Signature)
}
