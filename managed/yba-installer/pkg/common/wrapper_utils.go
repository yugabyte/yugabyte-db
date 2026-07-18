package common

import (
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/user"
	"path/filepath"
	"strings"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

/*
* Utility methods that allow us to log common actions
* like making dirs, copying files, etc
 */

func MkdirAll(path string, perm os.FileMode) error {
	log.Debug(fmt.Sprintf("Creating dir %s", path))
	err := os.Mkdir(path, os.ModePerm)
	if err == nil {
		// change modification bits as well
		if e := os.Chmod(path, perm); e != nil {
			return e
		}
		return nil
	} else if errors.Is(err, os.ErrExist) {
		log.Debug(fmt.Sprintf("Dir %s already exists. Skipping creation.", path))
		return nil
	} else if errors.Is(err, os.ErrPermission) {
		return fmt.Errorf("permission denied creating %s error: %w", path, err)
	} else if errors.Is(err, os.ErrInvalid) || strings.Contains(err.Error(), "invalid") {
		return fmt.Errorf("invalid path %s error: %w", path, err)
	} else if _, ok := err.(*os.PathError); ok {
		// recursive case where we need to make parent directories
		return mkdirAllHelper(filepath.Dir(path), []string{filepath.Base(path)}, perm)
	}
	return fmt.Errorf("unexpected error: %w", err)
}

func mkdirAllHelper(path string, children []string, perm os.FileMode) error {
	log.Debug("Creating directory " + path)
	if path == "/" {
		return fmt.Errorf("Can not create root directory.")
	}
	err := os.Mkdir(path, os.ModePerm)
	if err != nil {
		if errors.Is(err, os.ErrExist) {
			log.Debug(fmt.Sprintf("Dir %s already exists. Skipping creation.", path))
			return nil
		}
		if errors.Is(err, os.ErrPermission) {
			return fmt.Errorf("permission denied while creating %s error: %w", path, err)
		}
		if pathErr, ok := err.(*os.PathError); ok {
			// Specific handling of insufficient disk space or too many links
			if strings.Contains(strings.ToLower(pathErr.Err.Error()), "no space") {
				return fmt.Errorf("insufficient disk space at %s error: %w", path, pathErr.Err)
			}
			if strings.Contains(strings.ToLower(pathErr.Err.Error()), "too many links") {
				return fmt.Errorf("file system limit reached at %s error: %w", path, pathErr.Err)
			}
			if errors.Is(pathErr, os.ErrNotExist) {
				// Directory can not be created at this level. More parents must be created.
				// Appending to list so first directory that can be created will be at the end.
				return mkdirAllHelper(filepath.Dir(path), append(children, filepath.Base(path)), perm)
			}
		}
		return fmt.Errorf("unexpected error: %w", err)
	}
	// change modification bits as well
	if e := os.Chmod(path, perm); e != nil {
		return fmt.Errorf("error changing %s permissions to %d err: %w", path, perm, e)
	}
	// Also make children
	// Iterate in reverse because we appended all children starting with the deepest
	// so first child is last.
	for i := len(children) - 1; i >= 0; i-- {
		path = path + string(os.PathSeparator) + children[i]
		log.Debug("Creating directory " + path)
		if e := os.Mkdir(path, os.ModePerm); err != nil && !errors.Is(err, os.ErrExist) {
			return fmt.Errorf("error making %s error: %w", path, e)
		}
		// change modification bits as well
		if e := os.Chmod(path, perm); e != nil {
			return fmt.Errorf("error changing %s permissions to %d error: %w", path, perm, e)
		}
	}
	return nil
}

func Rename(src string, dst string) error {
	log.Debug(fmt.Sprintf("Moving file from %s -> %s", src, dst))
	err := os.Rename(src, dst)
	if err != nil {
		return fmt.Errorf("error renaming %s to %s: %s", src, dst, err.Error())
	}
	return nil
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
	if err := CopyFileError(src, dst); err != nil {
		log.Fatal("Error: " + err.Error())
	}
}

func CopyFileError(src string, dst string) error {

	log.Debug("Copying from " + src + " -> " + dst)

	bytesRead, errSrc := os.ReadFile(src)

	if errSrc != nil {
		return errSrc
	}
	errDst := os.WriteFile(dst, bytesRead, 0644)
	if errDst != nil {
		return errDst
	}
	return nil
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

// Util function to download files to tmp dir.
func DownloadFileToTmp(url string, name string) (string, error) {
	//Create file in tmp.
	filePath := fmt.Sprintf("/tmp/%s", name)
	file, err := os.Create(filePath)
	if err != nil {
		return "", err
	}
	defer file.Close()

	data, err := http.Get(url)
	if err != nil {
		return "", err
	}
	defer data.Body.Close()

	if data.StatusCode != http.StatusOK {
		return "", fmt.Errorf("Status not ok - %s", data.Status)
	}

	//Copy bits to the file.
	_, err = io.Copy(file, data.Body)
	return filePath, err
}
