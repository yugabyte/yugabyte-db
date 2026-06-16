// Copyright (c) YugabyteDB, Inc.

package task

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"strings"

	"golang.org/x/crypto/ssh"
)

// This struct represents the handler for the rotate SSH key task.
type RotateSSHKeyHandler struct {
	param    *pb.RotateSshKeyInput
	username string
	logOut   util.Buffer
}

// This function creates a new instance of the RotateSSHKeyHandler.
func NewRotateSSHKeyHandler(param *pb.RotateSshKeyInput, username string) *RotateSSHKeyHandler {
	return &RotateSSHKeyHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// This function returns the current task status.
func (handler *RotateSSHKeyHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info: util.NewBuffer(module.MaxBufferCapacity),
		ExitStatus: &ExitStatus{
			Error: util.NewBuffer(module.MaxBufferCapacity),
			Code:  0,
		},
	}
}

// This function returns the string representation of the task.
func (handler *RotateSSHKeyHandler) String() string {
	return "RotateSshKeyTask"
}

// This function handles the rotate SSH key task.
func (handler *RotateSSHKeyHandler) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	sshUser := handler.param.GetSshUser()
	if sshUser == "" {
		return nil, fmt.Errorf("SSH user is not set")
	}
	tmpDir := handler.param.GetRemoteTmp()
	if tmpDir == "" {
		return nil, fmt.Errorf("Remote tmp is not set")
	}
	// Old public key is to be removed if set. New public key is to be added if set.
	oldPublicKeyContent := strings.TrimSpace(handler.param.GetOldPublicKeyContent())
	newPublicKeyContent := strings.TrimSpace(handler.param.GetNewPublicKeyContent())
	if oldPublicKeyContent == "" && newPublicKeyContent == "" {
		// At least one of the public key contents must be set.
		return nil, fmt.Errorf("Old and new public key contents are not set")
	}
	userInfo, err := util.UserInfo(sshUser)
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	if userInfo.User.HomeDir == "" {
		return nil, fmt.Errorf("Home directory does not exist for user %s", sshUser)
	}
	var oldSshKey []byte
	var newSshKey []byte
	if oldPublicKeyContent != "" {
		// Parse to validate the key.
		_, _, _, _, err = ssh.ParseAuthorizedKey([]byte(oldPublicKeyContent))
		if err != nil {
			err = fmt.Errorf("Error parsing old public key: %s", err.Error())
			handler.logOut.WriteLine(err.Error())
			util.FileLogger().Error(ctx, err.Error())
			return nil, err
		}
		oldSshKey = []byte(oldPublicKeyContent)
	}
	if newPublicKeyContent != "" {
		// Parse to validate the key.
		_, _, _, _, err = ssh.ParseAuthorizedKey([]byte(newPublicKeyContent))
		if err != nil {
			err = fmt.Errorf("Error parsing new public key: %s", err.Error())
			handler.logOut.WriteLine(err.Error())
			util.FileLogger().Error(ctx, err.Error())
			return nil, err
		}
		newSshKey = []byte(newPublicKeyContent)
	}
	authorizedKeysPath := filepath.Join(userInfo.User.HomeDir, ".ssh", "authorized_keys")
	err = handler.rotateKey(ctx, userInfo, authorizedKeysPath, oldSshKey, newSshKey)
	if err != nil {
		return nil, err
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_RotateSshKeyOutput{
			RotateSshKeyOutput: &pb.RotateSshKeyOutput{},
		},
	}, nil
}

// This function rotates the SSH key.
func (handler *RotateSSHKeyHandler) rotateKey(
	ctx context.Context,
	userInfo *util.UserDetail,
	authorizedKeysPath string,
	oldSshKey []byte,
	newSshKey []byte,
) error {
	// Retrieve the existing keys from the authorized keys file.
	keys, err := handler.retrieveExistingKeys(ctx, authorizedKeysPath)
	if err != nil {
		return err
	}
	keysToRemain := [][]byte{}
	for _, key := range keys {
		if oldSshKey != nil && handler.areKeysSame(key, oldSshKey) {
			util.FileLogger().Info(ctx, "Removing old SSH key")
			handler.logOut.WriteLine("Removing old SSH key")
			continue
		}
		if newSshKey != nil && handler.areKeysSame(key, newSshKey) {
			util.FileLogger().Info(ctx, "New SSH key already exists. Appending to authorized keys")
			continue
		}
		keysToRemain = append(keysToRemain, key)
	}
	if newSshKey != nil {
		util.FileLogger().Info(ctx, "Adding new SSH key")
		handler.logOut.WriteLine("Adding new SSH key")
		keysToRemain = append(keysToRemain, newSshKey)
	}
	return handler.writeAuthorizedKeys(ctx, userInfo, authorizedKeysPath, keysToRemain)
}

// This function checks if two keys are the same, ignoring the comments.
func (handler *RotateSSHKeyHandler) areKeysSame(key1, key2 []byte) bool {
	if key1 == nil || key2 == nil || bytes.Equal(key1, key2) {
		return true
	}
	// This filters out the comments.
	publicKey1, _, _, _, err := ssh.ParseAuthorizedKey(key1)
	if err != nil {
		return false
	}
	// This filters out the comments.
	publicKey2, _, _, _, err := ssh.ParseAuthorizedKey(key2)
	if err != nil {
		return false
	}
	return bytes.Equal(publicKey1.Marshal(), publicKey2.Marshal())
}

// This function writes the authorized keys to a file.
func (handler *RotateSSHKeyHandler) writeAuthorizedKeys(
	ctx context.Context,
	userInfo *util.UserDetail,
	authorizedKeysPath string,
	keys [][]byte,
) error {
	remoteTmp := handler.param.GetRemoteTmp()
	if remoteTmp == "" {
		return fmt.Errorf("Remote tmp is not set")
	}
	// Create a temporary file to write the authorized keys to.
	tmpFile, err := os.CreateTemp(remoteTmp, "authorized_keys_*")
	if err != nil {
		err = fmt.Errorf("Error creating temp file: %s", err.Error())
		util.FileLogger().Error(ctx, err.Error())
		handler.logOut.WriteLine(err.Error())
		return err
	}
	defer os.Remove(tmpFile.Name())
	defer tmpFile.Close()
	for _, key := range keys {
		_, err = tmpFile.Write(key)
		if err != nil {
			err = fmt.Errorf("Error writing authorized key: %w", err)
			util.FileLogger().Error(ctx, err.Error())
			handler.logOut.WriteLine(err.Error())
			return err
		}
		tmpFile.WriteString("\n")
	}
	// Set the mode of the temporary file to 0666 to make cat work as a different user.
	tmpFile.Chmod(os.FileMode(0666))
	// Overwrite the authorized keys file, preserving the selinux context.
	cmd := fmt.Sprintf("cat '%s' > '%s'", tmpFile.Name(), authorizedKeysPath)
	_, err = module.RunShellCmd(ctx, handler.username, "CopyAuthorizedKeys", cmd, handler.logOut)
	if err != nil {
		err = fmt.Errorf("Error moving authorized keys: %w", err)
		util.FileLogger().Error(ctx, err.Error())
		handler.logOut.WriteLine(err.Error())
		return err
	}
	return nil
}

// This function retrieves the existing keys from the authorized keys file.
func (handler *RotateSSHKeyHandler) retrieveExistingKeys(
	ctx context.Context,
	authorizedKeysPath string,
) ([][]byte, error) {
	keys := [][]byte{}
	file, err := os.Open(authorizedKeysPath)
	if err != nil {
		return keys, fmt.Errorf("Error opening authorized keys file: %w", err)
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		// Skip empty/comment lines
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		keys = append(keys, []byte(line))
	}
	if err := scanner.Err(); err != nil {
		err = fmt.Errorf("Error parsing authorized key: %w", err)
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	return keys, nil
}
