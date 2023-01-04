package common

import (
	"fmt"
	"os"
	"os/user"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/logging"
)

/*
* Utility methods that allow us to log common actions
* like making dirs, copying files, etc
 */

func MkdirAll(path string, perm os.FileMode) error {
	log.Debug(fmt.Sprintf("Creating dir %s", path))
	return os.MkdirAll(path, perm)
}

func RenameOrFail(src string, dst string) {
	log.Debug(fmt.Sprintf("Moving file from %s -> %s", src, dst))
	err := os.Rename(src, dst)
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

}

// MkdirAllOrFail creates a directory according to the given permissions, logging an error if necessary.
func MkdirAllOrFail(dir string, perm os.FileMode) {
	err := MkdirAll(dir, perm)
	if err != nil && !os.IsExist(err) {
		log.Fatal(fmt.Sprintf("Error creating %s. Failed with %s", dir, err.Error()))
	}
}

// CopyFile copies src file to dst.
// Assumes both src/dst are valid absolute paths and dst file parent directory is already created.
func CopyFile(src string, dst string) {

	log.Debug("Copying from " + src + " -> " + dst)

	bytesRead, errSrc := os.ReadFile(src)

	if errSrc != nil {
		log.Fatal("Error: " + errSrc.Error() + ".")
	}
	errDst := os.WriteFile(dst, bytesRead, 0644)
	if errDst != nil {
		log.Fatal("Error: " + errDst.Error() + ".")
	}

}

func RemoveAll(path string) error {
	log.Debug(fmt.Sprintf("Removing directory %s", path))
	return os.RemoveAll(path)
}

func GetCurrentUser() string {
	user, err := user.Current()
	if err != nil {
		log.Fatal(fmt.Sprintf("Error %s getting current user", err.Error()))
	}
	return user.Username
}
