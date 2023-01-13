package license

import (
	"crypto/sha256"
	"encoding/base64"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/common"
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
	if _, err := os.Stat(common.LicenseFileInstall); errors.Is(err, fs.ErrNotExist) {
		return nil, err
	}
	return FromFile(common.LicenseFileInstall)
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
			err = fmt.Errorf("cannot write license to %s - already exists", newLocation)
		}
		return err
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
