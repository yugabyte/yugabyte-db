// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"io/fs"
	"node-agent/util"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/nikolalohinski/gonja/v2"
	"github.com/nikolalohinski/gonja/v2/exec"
)

// Custom filters for Gonja templating engine.
var customFilterFunctions = map[string]exec.FilterFunction{
	"split_string": splitString,
	"bool":         convertBool,
}

// Custom tests for Gonja templating engine.
// The original ones do not handle type coercion between strings and booleans.
var customTestFunctions = map[string]exec.TestFunction{
	"true":  matchBool(true),  // This adds "x is true" in the template.
	"false": matchBool(false), // This adds "x is false" in the template.
}

func init() {
	filters := gonja.DefaultEnvironment.Filters
	for name, fnc := range customFilterFunctions {
		if err := filters.Register(name, fnc); err != nil {
			util.FileLogger().
				Fatalf(context.TODO(), "Error registering custom filter %s: %s", name, err.Error())
		}
	}
	tests := gonja.DefaultEnvironment.Tests
	for name, fnc := range customTestFunctions {
		if err := tests.Register(name, fnc); err != nil {
			util.FileLogger().
				Fatalf(context.TODO(), "Error registering custom test %s: %s", name, err.Error())
		}
	}
}

// Custom filter to split a comma separated string into a list.
func splitString(e *exec.Evaluator, in *exec.Value, params *exec.VarArgs) *exec.Value {
	if in.IsError() {
		return in
	}
	separator := ","
	if !params.First().IsNil() {
		separator = params.First().String()
	}
	value := in.String()
	value = strings.Trim(value, "\"")
	tokens := strings.Split(value, separator)
	for i := range tokens {
		tokens[i] = strings.TrimSpace(tokens[i])
	}
	return exec.AsValue(tokens)
}

// Custom filter to convert a value to boolean with type coercion.
func convertBool(e *exec.Evaluator, in *exec.Value, params *exec.VarArgs) *exec.Value {
	if in.IsError() {
		return in
	}
	if in.IsBool() {
		return in
	}
	if in.IsString() {
		b, err := strconv.ParseBool(strings.TrimSpace(in.String()))
		if err == nil {
			return exec.AsValue(b)
		}
	}
	// Let the template engine handle the error.
	return exec.AsValue(in.Bool())
}

// Custom test to match boolean values with type coercion.
func matchBool(target bool) exec.TestFunction {
	return exec.TestFunction(
		func(ctx *exec.Context, in *exec.Value, params *exec.VarArgs) (bool, error) {
			b, ok := asBoolIfPossible(in.Interface())
			if !ok {
				return false, nil
			}
			return b == target, nil
		},
	)
}

// Helper function to convert interface{} to bool if possible.
func asBoolIfPossible(val interface{}) (bool, bool) {
	if b, ok := val.(bool); ok {
		return b, true
	}
	if str, ok := val.(string); ok {
		parsed, err := strconv.ParseBool(str)
		if err == nil {
			return parsed, true
		}
	}
	return false, false
}

func CopyFile(
	ctx context.Context,
	values map[string]any,
	templateSubpath, destination string,
	mod fs.FileMode,
	username string,
) (string, error) {
	userDetail, err := util.UserInfo(username)
	if err != nil {
		return "", err
	}
	templatePath := templateSubpath
	if !filepath.IsAbs(templateSubpath) {
		templatePath = filepath.Join(util.TemplateDir(), templateSubpath)
	}
	util.FileLogger().Infof(ctx, "Resolving template file %s", templatePath)
	output, err := ResolveTemplate(ctx, values, templatePath)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Resolution failed for template file %s", templatePath)
		return "", err
	}
	output = strings.TrimSpace(output)
	file, err := os.OpenFile(destination, os.O_TRUNC|os.O_RDWR|os.O_CREATE, mod)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in creating file %s - %s", destination, err.Error())
		return "", err
	}
	defer file.Close()
	if !userDetail.IsCurrent {
		err = file.Chown(int(userDetail.UserID), int(userDetail.GroupID))
		if err != nil {
			util.FileLogger().
				Errorf(ctx, "Error in changing file owner %s - %s", destination, err.Error())
			return "", err
		}
	}
	_, err = file.WriteString(output)
	if err != nil {
		return "", err
	}
	return output, nil
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
	return tpl.ExecuteToString(exec.NewContext(values))
}
