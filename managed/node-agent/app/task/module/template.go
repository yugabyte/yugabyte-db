// Copyright (c) YugaByte, Inc.

package module

import (
	"context"
	"io/fs"
	"node-agent/util"
	"os"
	"path/filepath"

	"github.com/noirbizarre/gonja"
)

func CopyFile(
	ctx context.Context,
	values map[string]any,
	templateSubpath, destination string,
	mod fs.FileMode,
) error {
	templatePath := filepath.Join(util.TemplateDir(), templateSubpath)
	output, err := ResolveTemplate(ctx, values, templatePath)
	if err != nil {
		return err
	}
	err = os.WriteFile(destination, []byte(output), mod)
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
