// Copyright 2016 The CMux Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.

// The file is modified for YugaByte, Inc.

package cmux

import (
	"bufio"
	"io"
	"net/http"
	"strings"
)

// This is enough for our use-case.
const maxHTTPRead = 50

// Any is a Matcher that matches any connection.
func Any() Matcher {
	return func(r io.Reader) bool { return true }
}

// HTTP1 parses the header to look for HTTP1 version.
func HTTP1() Matcher {
	return func(r io.Reader) bool {
		br := bufio.NewReader(&io.LimitedReader{R: r, N: maxHTTPRead})
		l, part, err := br.ReadLine()
		if err != nil || part {
			return false
		}
		_, _, proto, ok := parseRequestLine(string(l))
		if !ok {
			return false
		}

		v, _, ok := http.ParseHTTPVersion(proto)
		return ok && v == 1
	}
}

func parseRequestLine(line string) (method, uri, proto string, ok bool) {
	s1 := strings.Index(line, " ")
	if s1 < 0 {
		return
	}
	s2 := strings.Index(line[s1+1:], " ")
	if s2 < 0 {
		return
	}
	s2 += s1 + 1
	return line[:s1], line[s1+1 : s2], line[s2+1:], true
}
