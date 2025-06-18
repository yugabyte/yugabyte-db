// Copyright (c) YugaByte, Inc.

package module

import (
	"context"
	"io/fs"
	"node-agent/util"
	"os"
	"path/filepath"

	"github.com/nikolalohinski/gonja"
)

func CopyFile(
	ctx context.Context,
	values map[string]any,
	templateSubpath, destination string,
	mod fs.FileMode,
	username string,
) error {
	userDetail, err := util.UserInfo(username)
	if err != nil {
		return err
	}
	templatePath := filepath.Join(util.TemplateDir(), templateSubpath)
	util.FileLogger().Infof(ctx, "Resolving template file %s", templatePath)
	output, err := ResolveTemplate(ctx, values, templatePath)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Resolution failed for template file %s", templatePath)
		return err
	}
	file, err := os.OpenFile(destination, os.O_TRUNC|os.O_RDWR|os.O_CREATE, mod)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in creating file %s - %s", destination, err.Error())
		return err
	}
	defer file.Close()
	if !userDetail.IsCurrent {
		err = file.Chown(int(userDetail.UserID), int(userDetail.GroupID))
		if err != nil {
			util.FileLogger().
				Errorf(ctx, "Error in changing file owner %s - %s", destination, err.Error())
			return err
		}
	}
	_, err = file.WriteString(output)
	if err != nil {
		return err
	}
	return nil
}

func ResolveTemplate(
	ctx context.Context,
	values map[string]any,
	templatePath string,
) (string, error) {
	tpl, err := gonja.FromFile(templatePath)
	if err != nil {
		return "", err
	}
	return tpl.Execute(values)
}
