/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

SET extra_float_digits = 0;
LOAD 'age';
SET search_path TO ag_catalog;

SELECT create_graph('scan');

SELECT create_graph('scan');

--
-- multi-line comment
--

SELECT * FROM cypher('scan', $$
/*
 * multi-line comment
 */
RETURN 0
/**/
$$) AS t(a agtype);

SELECT * FROM cypher('scan', $$
/* unterminated /* comment
RETURN 0
$$) AS t(a int);
-- recover syntax highlighting */

--
-- single-line comment
--

SELECT * FROM cypher('scan', $$
// single-line
// comment
RETURN 0
$$) AS t(a agtype);

--
-- decimal integer
--

SELECT * FROM cypher('scan', $$
RETURN 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
$$) AS t(a agtype, b agtype, c agtype, d agtype, e agtype,
         f agtype, g agtype, h agtype, i agtype, j agtype);

SELECT * FROM cypher('scan', $$
RETURN 11, 22, 33, 44, 55, 66, 77, 88, 99
$$) AS t(a agtype, b agtype, c agtype, d agtype, e agtype,
         f agtype, g agtype, h agtype, i agtype);

-- 2^31 - 1, 2^31
SELECT * FROM cypher('scan', $$
RETURN 2147483647, 2147483648
$$) AS t(a agtype, b agtype);

--
-- octal integer
--

SELECT * FROM cypher('scan', $$
RETURN 00, 01, 02, 03, 04, 05, 06, 07, 010
$$) AS t(a agtype, b agtype, c agtype, d agtype, e agtype,
         f agtype, g agtype, h agtype, i agtype);

SELECT * FROM cypher('scan', $$
RETURN 000, 011, 022, 033, 044, 055, 066, 077
$$) AS t(a agtype, b agtype, c agtype, d agtype, e agtype,
         f agtype, g agtype, h agtype);

-- 2^31 - 1, 2^31
SELECT * FROM cypher('scan', $$
RETURN 000000000000, 017777777777, 0020000000000
$$) AS t(a agtype, b agtype, c agtype);

-- 2^60 - 1, 2^64 - 1
SELECT * FROM cypher('scan', $$
RETURN 077777777777777777777, 01777777777777777777777
$$) AS t(a agtype, b agtype);

-- an invalid character after reading valid digits
SELECT * FROM cypher('scan', $$
RETURN 012345678
$$) AS t(a int);

-- an invalid character after the leading "0"
SELECT * FROM cypher('scan', $$
RETURN 09
$$) AS t(a int);

--
-- hexadecimal integer
--

SELECT * FROM cypher('scan', $$
RETURN 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9
$$) AS t(a agtype, b agtype, c agtype, d agtype, e agtype,
         f agtype, g agtype, h agtype, i agtype, j agtype);

SELECT * FROM cypher('scan', $$
RETURN 0xA, 0xB, 0xC, 0xD, 0xE, 0xF
$$) AS t(a agtype, b agtype, c agtype, d agtype, e agtype, f agtype);

SELECT * FROM cypher('scan', $$
RETURN 0X00, 0X11, 0X22, 0X33, 0X44, 0X55, 0X66, 0X77, 0X88, 0X99
$$) AS t(a agtype, b agtype, c agtype, d agtype, e agtype,
         f agtype, g agtype, h agtype, i agtype, j agtype);

SELECT * FROM cypher('scan', $$
RETURN 0XAa, 0XBb, 0XCc, 0XDd, 0XEe, 0xFf
$$) AS t(a agtype, b agtype, c agtype, d agtype, e agtype, f agtype);

-- 2^31 - 1, 2^31
SELECT * FROM cypher('scan', $$
RETURN 0x00000000, 0x7FFFFFFF, 0x080000000
$$) AS t(a agtype, b agtype, c agtype);

-- 10^18, 2^64 - 1
SELECT * FROM cypher('scan', $$
RETURN 0xde0b6b3a7640000, 0xffffffffffffffff
$$) AS t(a agtype, b agtype);

-- an invalid character after reading valid digits
SELECT * FROM cypher('scan', $$
RETURN 0xF~
$$) AS t(a int);

-- an invalid character after the leading "0x"
SELECT * FROM cypher('scan', $$
RETURN 0x~
$$) AS t(a int);

-- "0x" followed by nothing
SELECT * FROM cypher('scan', $$
RETURN 0x
$$) AS t(a int);

--
-- decimal
--

SELECT * FROM cypher('scan', $$
RETURN 03., 3.141592, .141592
$$) AS t(a agtype, b agtype, c agtype);

-- "0" and ".."
SELECT * FROM cypher('scan', $$
RETURN 0..
$$) AS t(a text, b text, c text);

--
-- scientific notation
--

SELECT * FROM cypher('scan', $$
RETURN 3141592e-6, 3.141592E0, .3141592e+1
$$) AS t(a agtype, b agtype, c agtype);

-- invalid exponent parts

SELECT * FROM cypher('scan', $$
RETURN 3141592e-
$$) AS t(a text);

SELECT * FROM cypher('scan', $$
RETURN 3.141592E
$$) AS t(a text);

SELECT * FROM cypher('scan', $$
RETURN .3141592e+
$$) AS t(a text);

--
-- quoted string
--

-- a long string
SELECT * FROM cypher('scan', $$
RETURN " !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
$$) AS t(a agtype);

-- escape sequences
SELECT * FROM cypher('scan', $$
RETURN " \" \" ' \' ", ' \' \' " \" ', " / \/ \\ \b \f \n \r \t "
$$) AS t(a agtype, b agtype, c agtype);

-- invalid escape sequence
SELECT * FROM cypher('scan', $$
RETURN "\a"
$$) AS t(a text);

-- Unicode escape sequences
SELECT * FROM cypher('scan', $$
RETURN "\u03A9 (GREEK CAPITAL LETTER OMEGA, U+03A9, Ω)",
       "\U0001d6e9 (MATHEMATICAL ITALIC CAPITAL THETA, U+1D6E9, 𝛩)",
       "\ud835\U0000DEF0 (MATHEMATICAL ITALIC CAPITAL OMICRON, U+1D6F0, 𝛰)",
       "\u002E (FULL STOP, U+002E, .)"
$$) AS t(a agtype, b agtype, c agtype, d agtype);

-- invalid Unicode surrogate pair (need a low surrogate)
SELECT * FROM cypher('scan', $$
RETURN "\uD835"
$$) AS t(a text);

-- invalid Unicode surrogate pair (not a low surrogate)
SELECT * FROM cypher('scan', $$
RETURN "\uD835\u002E"
$$) AS t(a text);

-- invalid Unicode surrogate pair (need a high surrogate)
SELECT * FROM cypher('scan', $$
RETURN "\uDEF0"
$$) AS t(a text);

-- invalid Unicode escape value (must be less than or equal to 10FFFF)
SELECT * FROM cypher('scan', $$
RETURN "\U00110000"
$$) AS t(a text);

-- unsupported Unicode escape value ('\0' is not allowed)
SELECT * FROM cypher('scan', $$
RETURN "\u0000"
$$) AS t(a text);

-- unsupported Unicode escape value (the server encoding is not UTF8)

CREATE DATABASE contrib_regression_age_euc_kr
  TEMPLATE template0
  ENCODING EUC_KR
  LC_COLLATE 'C' LC_CTYPE 'C';

\c contrib_regression_age_euc_kr

CREATE EXTENSION age;
LOAD 'age';
SET search_path TO ag_catalog;
SELECT create_graph('scan');

SELECT * FROM cypher('scan', $$
RETURN "\U0001D706"
$$) AS t(a text);

SELECT drop_graph('scan', true);

\c contrib_regression

DROP DATABASE contrib_regression_age_euc_kr;

LOAD 'age';
SET search_path TO ag_catalog;

-- invalid Unicode escape sequence (must be \uXXXX or \UXXXXXXXX)

SELECT * FROM cypher('scan', $$
RETURN "\UD835"
$$) AS t(a text);

SELECT * FROM cypher('scan', $$
RETURN "\uD835\uDEF"
$$) AS t(a text);

-- unterminated quoted strings

SELECT * FROM cypher('scan', $$RETURN "unterminated quoted string$$) AS t(a text);
-- recover syntax highlighting "

SELECT * FROM cypher('scan', $$RETURN 'unterminated quoted string$$) AS t(a text);
-- recover syntax highlighting '

SELECT * FROM cypher('scan', $$RETURN "escape \$$) AS t(a text);
-- recover syntax highlighting "

SELECT * FROM cypher('scan', $$RETURN "high surrogate \uD835$$) AS t(a text);
-- recover syntax highlighting "

--
-- identifier
--

-- check that they are accepted as identifier (all tests throw an error)

SELECT * FROM cypher('scan', $$
RETURN _$09A_z
$$) AS t(id text);

SELECT * FROM cypher('scan', $$
RETURN A
$$) AS t(id text);

SELECT * FROM cypher('scan', $$
RETURN z
$$) AS t(id text);

SELECT * FROM cypher('scan', $$
RETURN `$`
$$) AS t(id text);

SELECT * FROM cypher('scan', $$
RETURN `0`
$$) AS t(id text);

SELECT * FROM cypher('scan', $$
RETURN ````
$$) AS t(id text);

-- zero-length quoted identifier
SELECT * FROM cypher('scan', $$
RETURN ``
$$) AS t(a text);

SELECT * FROM cypher('scan', $$
RETURN `unterminated quoted identifier
$$) AS t(a text);
-- recover syntax highlighting `

--
-- parameter
--

-- invalid parameter names

SELECT * FROM cypher('scan', $cypher$
RETURN $$
$cypher$) AS t(a text);

SELECT * FROM cypher('scan', $$
RETURN $0
$$) AS t(a text);

SELECT drop_graph('scan', true);
