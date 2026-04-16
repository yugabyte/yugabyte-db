package testutils

import (
	"encoding/json"
	"os"
	"path/filepath"
	"regexp"
	"time"
)

const (
	botAccessKeyRegex = ".*_bot_access_key.json"
	awsAccessKeyEnv   = "AWS_ACCESS_KEY_ID"
	awsSecretKey      = "AWS_SECRET_ACCESS_KEY"
)

type accessKey struct {
	UserName        string    `json:"UserName"`
	AccessKeyId     string    `json:"AccessKeyId"`
	Status          string    `json:"Status"`
	SecretAccessKey string    `json:"SecretAccessKey"`
	CreateDate      time.Time `json:"CreateDate"`
}

type botAccessKey struct {
	AccessKey accessKey `json:"AccessKey"`
}

func authFromBotAccessJson() (awsAuthentication, error) {
	fileDir, err := getBotAccessDir()
	if err != nil {
		return awsAuthentication{}, err
	}
	regex, err := regexp.Compile(botAccessKeyRegex)
	if err != nil {
		return awsAuthentication{}, err
	}
	files, err := os.ReadDir(fileDir)
	if err != nil {
		return awsAuthentication{}, err
	}
	for _, file := range files {
		if regex.MatchString(file.Name()) {
			filePath := filepath.Join(fileDir, file.Name())
			data, err := os.ReadFile(filePath)
			if err != nil {
				return awsAuthentication{}, err
			}
			var botAccessKey botAccessKey
			err = json.Unmarshal(data, &botAccessKey)
			if err != nil {
				return awsAuthentication{}, err
			}
			return awsAuthentication{
				AccessKeyId:     botAccessKey.AccessKey.AccessKeyId,
				SecretAccessKey: botAccessKey.AccessKey.SecretAccessKey,
			}, nil
		}
	}
	return awsAuthentication{}, nil
}

func authFromEnv() awsAuthentication {
	return awsAuthentication{
		AccessKeyId:     os.Getenv(awsAccessKeyEnv),
		SecretAccessKey: os.Getenv(awsSecretKey),
	}
}

func getBotAccessDir() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".yugabyte"), nil
}
