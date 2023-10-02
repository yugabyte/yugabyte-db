package yugaware

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

const versionMetadataJSON = "version_metadata.json"

// NotInstalledVersionError to return when yba is not installed.
var NotInstalledVersionError = fmt.Errorf("not installed")

type versionJSON struct {
	Version     string `json:"version_number"`
	BuildNumber string `json:"build_number"`
	BuildID     string `json:"build_id"`
}

// ParseVersion compiles our version number from the version_metadata.json
func (vj versionJSON) ParseVersion() (version string, err error) {
	if vj.BuildNumber == "PRE_RELEASE" && os.Getenv("YBA_MODE") == "dev" {
		// hack to allow testing dev itest builds
		version = vj.Version + "-b" + vj.BuildID
	} else {
		version = vj.Version + "-b" + vj.BuildNumber
	}

	if !common.IsValidVersion(version) {
		err = fmt.Errorf("Invalid version in metadata file '%s'", version)
	}
	return version, err
}

// InstalledVersionFromMetadata gets the version of yba by parsing the version json from the install
// root.
func InstalledVersionFromMetadata() (string, error) {
	jsonPath := filepath.Join(common.GetActiveSymlink(), "yba_installer", versionMetadataJSON)
	jsonFile, err := os.Open(jsonPath)
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return "", NotInstalledVersionError
		}
		return "", err
	}

	var vj versionJSON
	if err := json.NewDecoder(jsonFile).Decode(&vj); err != nil {
		return "", err
	}
	return vj.ParseVersion()
}
