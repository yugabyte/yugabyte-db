// Copyright (c) YugabyteDB, Inc.

package config

import (
	"bytes"
	"fmt"
	"os"
	"strings"

	"github.com/santhosh-tekuri/jsonschema/v6"
	"gopkg.in/yaml.v3"
)

// SchemaHandler handles the YNP config schema validation and template generation.
type SchemaHandler struct {
	schema *jsonschema.Schema
}

func NewSchemaHandler(schemaPath string) (*SchemaHandler, error) {
	compiler := jsonschema.NewCompiler()
	schema, err := compiler.Compile(schemaPath)
	if err != nil {
		return nil, fmt.Errorf("Failed to load YNP config schema: %v", err)
	}
	return &SchemaHandler{schema: schema}, nil
}

// ValidateYnpConfig validates the provided config against the schema.
func (s *SchemaHandler) ValidateYnpConfig(config map[string]map[string]any) error {
	m := make(map[string]any)
	for section, values := range config {
		m[section] = values
	}
	err := s.schema.Validate(m)
	if err != nil {
		return fmt.Errorf("Failed to validate config - %v", err.Error())
	}
	return nil
}

// Returns the sub-schema for the given path.
// E.g. path "yba.url" returns the schema for the url key under yba section.
func (s *SchemaHandler) SubSchema(path string) (*jsonschema.Schema, error) {
	schema := s.schema
	if schema == nil {
		return nil, fmt.Errorf("YNP config schema is not loaded")
	}
	parts := strings.Split(path, ".")
	for i := range parts {
		part := strings.TrimSpace(parts[i])
		sch, ok := schema.Properties[part]
		if !ok || schema == nil {
			return nil, fmt.Errorf(
				"Invalid config path %s. Missing section/key %s in ynp config",
				path,
				part,
			)
		}
		schema = sch
	}
	return schema, nil
}

// OverrideProperties overrides the values in targetConfig based on the provided overrides list.
// The override format is field1.field2.field3=value and key must already exist in the targetConfig.
func (s *SchemaHandler) OverrideProperties(
	overrides []string,
	targetConfig map[string]map[string]any,
) error {
	for _, override := range overrides {
		// E.g field1.field2.field3=value.
		parts := strings.SplitN(override, "=", 2)
		if len(parts) != 2 {
			return fmt.Errorf(
				"Invalid config override format %s. Expected format is field1.field2.field3=value",
				override,
			)
		}
		fields := strings.Split(parts[0], ".")
		if len(fields) < 2 {
			return fmt.Errorf(
				"Invalid config override format %s. Path must be at least two levels, separated by dots",
				override,
			)
		}
		// Get the top-level field to start from map[string]any.
		topProperty := strings.TrimSpace(fields[0])
		m, ok := targetConfig[topProperty]
		if !ok {
			return fmt.Errorf(
				"Invalid config override path %s. Top level property %s doesn't exist in ynp config",
				override,
				topProperty,
			)
		}
		currConfig := m
		fields = fields[1:]
		for i := range fields {
			// First verify the path exists in the config at each level, then override the value at the end.
			field := strings.TrimSpace(fields[i])
			value, ok := currConfig[field]
			if !ok {
				return fmt.Errorf(
					"Invalid config override path %s. Missing section/key %s in ynp config",
					override,
					field,
				)
			}
			if i == len(fields)-1 {
				// Terminal/leaf is reached. Unmarshal the value to get the correct type
				// and validate against the schema.
				value, err := jsonschema.UnmarshalJSON(strings.NewReader(parts[1]))
				if err != nil {
					return fmt.Errorf(
						"Failed to unmarshal config override %s: %s. String types may need escaped quotes",
						override,
						err.Error(),
					)
				}
				// Get the sub-schema to validate the override value.
				schema, err := s.SubSchema(parts[0])
				if err != nil {
					return err
				}
				// Validate the override value against the schema.
				if err := schema.Validate(value); err != nil {
					return fmt.Errorf("Failed to validate config override %s: %v", override, err)
				}
				// All good, update the value in the config.
				currConfig[field] = value
			} else {
				if m, ok := value.(map[string]any); ok {
					currConfig = m
				} else {
					return fmt.Errorf("Invalid config override path %s at %s. Only objects are overridable", override, field)
				}
			}
		}
	}
	return nil
}

// GenerateTemplateYAML generates the template YAML file from the schema and writes to the provided output file path.
// The placeholder name is a concatenation of the path in the schema with underscores.
func (s *SchemaHandler) GenerateTemplateYAML(outFile string) error {
	str, err := s.generateTemplateYAML()
	if err != nil {
		return fmt.Errorf("Failed to generate template YAML: %v", err)
	}
	// Finally, replace the placeholder with jinja template syntax.
	str = strings.ReplaceAll(str, "__OPEN__", "{{")
	str = strings.ReplaceAll(str, "__CLOSE__", "}}")
	err = os.WriteFile(outFile, []byte(str), 0644)
	if err != nil {
		return fmt.Errorf("Failed to write generated config file: %v", err)
	}
	return nil
}

func (s *SchemaHandler) generateTemplateYAML() (string, error) {
	if s.schema == nil {
		return "", fmt.Errorf("Schema not loaded")
	}
	node, err := buildDefaultFromSchema(s.schema, "")
	if err != nil {
		return "", fmt.Errorf("Failed to build default config from schema: %v", err)
	}
	buffer := &bytes.Buffer{}
	enc := yaml.NewEncoder(buffer)
	enc.SetIndent(2)
	err = enc.Encode(node)
	if err != nil {
		return "", fmt.Errorf("Failed to encode generated config to YAML: %v", err)
	}
	enc.Close()
	return buffer.String(), nil
}

func KeyPrefix(previous, current string) string {
	key := previous
	if key == "" {
		return current
	}
	return key + "_" + current
}

// buildDefaultFromSchema recursively builds a map[string]interface{} using the 'comment' field.
func buildDefaultFromSchema(schema *jsonschema.Schema, name string) (*yaml.Node, error) {
	sType := schema.Types.String()
	switch sType {
	case "[object]":
		// If no type is specified, try to infer from properties/items
		if len(schema.Properties) > 0 {
			nodes := make(map[string]*yaml.Node)
			for k, v := range schema.Properties {
				// Create the values.
				key := KeyPrefix(name, k)
				node, err := buildDefaultFromSchema(v, key)
				if err != nil {
					return nil, err
				}
				nodes[k] = node
			}
			node := yaml.Node{
				Kind: yaml.MappingNode,
			}
			for k, v := range nodes {
				// Create the key with the description as comment.
				keyNode := &yaml.Node{
					Kind:        yaml.ScalarNode,
					Value:       k,
					HeadComment: schema.Properties[k].Description,
				}
				node.Content = append(node.Content, keyNode, v)
			}
			return &node, nil
		}
	case "[array]":
		if ss, ok := schema.Items.([]*jsonschema.Schema); ok {
			nodes := make([]*yaml.Node, len(ss))
			for i, item := range ss {
				key := KeyPrefix(name, fmt.Sprintf("%d", i))
				node, err := buildDefaultFromSchema(item, key)
				if err != nil {
					return nil, err
				}
				nodes[i] = node
			}
			return &yaml.Node{
				Kind:    yaml.SequenceNode,
				Content: nodes,
			}, nil
		}
		if sch, ok := schema.Items.(*jsonschema.Schema); ok {
			node, err := buildDefaultFromSchema(sch, name)
			if err != nil {
				return nil, err
			}
			return node, nil
		}
	default:
		// For primitive types, use the name to create a placeholder value.
		// Generate a value like __OPEN__ section_key __CLOSE__.
		// Braces are special characters in YAML.
		value := fmt.Sprintf("__OPEN__ %s __CLOSE__", name)
		var node yaml.Node
		err := node.Encode(value)
		if err != nil {
			return nil, err
		}
		return &node, nil
	}
	return nil, fmt.Errorf("Unsupported schema type: %s", sType)
}
