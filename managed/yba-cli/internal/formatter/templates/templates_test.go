// Use of this source code is governed by an Apache 2.0-style
// license that can be found in the LICENSE file.

package templates

import (
	"bytes"
	"testing"

	"gotest.tools/v3/assert"
	is "gotest.tools/v3/assert/cmp"
)

// GitHub #32120
func TestParseJSONFunctions(t *testing.T) {
	tm, err := Parse(`{{json .Ports}}`)
	assert.NilError(t, err)

	var b bytes.Buffer
	assert.NilError(t, tm.Execute(&b, map[string]string{"Ports": "0.0.0.0:2->8/udp"}))
	want := "\"0.0.0.0:2->8/udp\""
	assert.Check(t, is.Equal(want, b.String()))
}

func TestParseStringFunctions(t *testing.T) {
	tm, err := Parse(`{{join "/" (splitList ":" .) }}`)
	assert.NilError(t, err)
	var b bytes.Buffer
	assert.NilError(t, tm.Execute(&b, "text:with:colon"))
	want := "text/with/colon"
	assert.Check(t, is.Equal(want, b.String()))
}

func TestNewParse(t *testing.T) {
	tm, err := NewParse("foo", "this is a {{ . }}")
	assert.NilError(t, err)

	var b bytes.Buffer
	assert.NilError(t, tm.Execute(&b, "string"))
	want := "this is a string"
	assert.Check(t, is.Equal(want, b.String()))
}

func TestParseTruncateFunction(t *testing.T) {
	source := "tupx5xzf6hvsrhnruz5cr8gwp"

	testCases := []struct {
		template string
		expected string
	}{
		{
			template: `{{truncate . 5}}`,
			expected: "tupx5",
		},
		{
			template: `{{truncate . 25}}`,
			expected: "tupx5xzf6hvsrhnruz5cr8gwp",
		},
		{
			template: `{{truncate . 30}}`,
			expected: "tupx5xzf6hvsrhnruz5cr8gwp",
		},
		{
			template: `{{pad . 3 3}}`,
			expected: "   tupx5xzf6hvsrhnruz5cr8gwp   ",
		},
	}

	for _, testCase := range testCases {
		testCase := testCase

		tm, err := Parse(testCase.template)
		assert.NilError(t, err)

		t.Run("Non Empty Source Test with template: "+testCase.template, func(t *testing.T) {
			var b bytes.Buffer
			assert.NilError(t, tm.Execute(&b, source))
			assert.Check(t, is.Equal(testCase.expected, b.String()))
		})

		t.Run("Empty Source Test with template: "+testCase.template, func(t *testing.T) {
			var c bytes.Buffer
			assert.NilError(t, tm.Execute(&c, ""))
			assert.Check(t, is.Equal("", c.String()))
		})

		t.Run("Nil Source Test with template: "+testCase.template, func(t *testing.T) {
			var c bytes.Buffer
			assert.Check(t, tm.Execute(&c, nil) != nil)
			assert.Check(t, is.Equal("", c.String()))
		})
	}
}
