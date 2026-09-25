//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/util/password_redaction.h"

#include <cctype>

namespace yb {
namespace ql {

// This is deliberately independent of the statement type: it is applied to raw statement text on
// paths that have no parse tree to check the opcode against (a syntax error, a failed PREPARE, a
// rejected CREATE/ALTER ROLE), and to the statement echoed into every SQL error message.
//
// The scan is token-aware rather than a plain regex: it skips over the constructs where a lone `'`
// is not a string delimiter -- double-quoted identifiers, -- and /* */ comments, $tag$ dollar
// strings, and E'..\'..' escape strings -- so an apostrophe inside any of them (e.g. a role named
// "o'brien" or a `/* don't */` comment) can't shift the following PASSWORD clause out of alignment
// and leak it. The token rules mirror scanner_lex.l; a divergence that makes the scanner misjudge
// where a token starts can leak a password, so keep them in sync.
std::string RedactPasswordLiterals(
    const std::string& operation, std::vector<RedactedRange>* ranges) {
  const size_t n = operation.size();
  std::string result;
  result.reserve(n + 16);
  bool redacted = false;

  const auto lower = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };

  // scanner_lex.l `ident_cont`: a character that continues an unquoted identifier. `$` is one, so
  // a `$` or `E'` that follows such a character is part of the identifier, not a token start.
  const auto is_ident_cont = [](char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '$' || u >= 0x80;
  };
  const auto at_token_start = [&](size_t i) {
    return i == 0 || !is_ident_cont(operation[i - 1]);
  };

  // If a `dolqdelim` ($$ or $tag$, where the tag follows `dolq_start` / `dolq_cont`) starts at
  // operation[i], returns the index just past it; otherwise std::string::npos.
  const auto dollar_delim_end = [&](size_t i) -> size_t {
    if (i >= n || operation[i] != '$') {
      return std::string::npos;
    }
    size_t j = i + 1;
    if (j < n) {
      const auto u = static_cast<unsigned char>(operation[j]);
      if (std::isalpha(u) || operation[j] == '_' || u >= 0x80) {
        ++j;
        while (j < n) {
          const auto v = static_cast<unsigned char>(operation[j]);
          if (!(std::isalnum(v) || operation[j] == '_' || v >= 0x80)) {
            break;
          }
          ++j;
        }
      }
    }
    return (j < n && operation[j] == '$') ? j + 1 : std::string::npos;
  };

  // Given the opening delimiter operation[i, delim_end), returns the index just past the matching
  // closing delimiter, or n if the dollar-quoted string is unterminated.
  const auto scan_dollar = [&](size_t i, size_t delim_end) -> size_t {
    const size_t close = operation.find(operation.data() + i, delim_end, delim_end - i);
    return close == std::string::npos ? n : close + (delim_end - i);
  };

  // Scans one single-quoted ('...') or escape (E'...') literal starting at the opening quote
  // operation[q]. In both, '' is an escaped quote; in an escape literal a backslash also escapes
  // the next character. Returns the index just past the closing quote, or n if the literal is
  // unterminated.
  const auto scan_string = [&](size_t q, bool escape) -> size_t {
    for (size_t i = q + 1; i < n; ++i) {
      const char c = operation[i];
      if (escape && c == '\\') {
        ++i;  // Skip the backslash-escaped character (loop ++i skips the backslash itself).
        continue;
      }
      if (c == '\'') {
        if (i + 1 < n && operation[i + 1] == '\'') {
          ++i;  // Escaped quote, still inside the literal.
          continue;
        }
        return i + 1;
      }
    }
    return n;
  };

  // If a string constant starts at operation[j], returns the index just past it (n if it is
  // unterminated); otherwise std::string::npos. Covers every form the lexer returns as SCONST --
  // '...', E'...', U&'...', $tag$...$tag$ -- plus the N/B/X-prefixed forms, which a rejected
  // statement may still carry. A quoted literal continues across `'...'<newline>'...'`
  // (`quotecontinue`), which the lexer joins into one constant, so the whole run is consumed.
  const auto sconst_end = [&](size_t j) -> size_t {
    if (j >= n) {
      return std::string::npos;
    }
    const size_t delim_end = dollar_delim_end(j);
    if (delim_end != std::string::npos) {
      return scan_dollar(j, delim_end);
    }
    bool escape = false;
    size_t quote = j;
    const char c = lower(operation[j]);
    if (c == 'u' && j + 2 < n && operation[j + 1] == '&') {
      quote = j + 2;
    } else if (c == 'e' || c == 'n' || c == 'b' || c == 'x') {
      escape = c == 'e';
      quote = j + 1;
    }
    if (quote >= n || operation[quote] != '\'') {
      return std::string::npos;
    }
    size_t end = scan_string(quote, escape);
    while (end < n) {
      // `quotecontinue`: whitespace (and -- comments) containing at least one newline, then '.
      size_t k = end;
      bool newline = false;
      while (k < n) {
        const char w = operation[k];
        if (w == '\n' || w == '\r') {
          newline = true;
          ++k;
        } else if (w == ' ' || w == '\t' || w == '\f') {
          ++k;
        } else if (w == '-' && k + 1 < n && operation[k + 1] == '-') {
          while (k < n && operation[k] != '\n' && operation[k] != '\r') {
            ++k;
          }
        } else {
          break;
        }
      }
      if (!newline || k >= n || operation[k] != '\'') {
        break;
      }
      end = scan_string(k, escape);
    }
    return end;
  };

  static const std::string kKeyword = "password";

  // Quote parity alone can't tell a string delimiter from an apostrophe that lives inside a
  // double-quoted identifier ("o'brien"), a comment (/* don't */), or a dollar-quoted string -- and
  // getting that wrong makes the real PASSWORD clause look like a mis-fired match and leak in
  // cleartext. So walk the statement token by token: copy comments, quoted identifiers, and string
  // constants that are not a password value verbatim, and redact the value of every top-level
  // `password = <string constant>` clause.
  size_t i = 0;
  while (i < n) {
    const char c = operation[i];

    // Line comment: -- to end of line.
    if (c == '-' && i + 1 < n && operation[i + 1] == '-') {
      size_t j = i + 2;
      while (j < n && operation[j] != '\n') {
        ++j;
      }
      result.append(operation, i, j - i);
      i = j;
      continue;
    }

    // Block comment: /* ... */, which nests in the CQL lexer.
    if (c == '/' && i + 1 < n && operation[i + 1] == '*') {
      size_t j = i + 2;
      int depth = 1;
      while (j < n && depth > 0) {
        if (operation[j] == '/' && j + 1 < n && operation[j + 1] == '*') {
          depth++;
          j += 2;
        } else if (operation[j] == '*' && j + 1 < n && operation[j + 1] == '/') {
          depth--;
          j += 2;
        } else {
          ++j;
        }
      }
      result.append(operation, i, j - i);
      i = j;
      continue;
    }

    // Double-quoted identifier: "..." with "" as an escaped quote.
    if (c == '"') {
      size_t j = i + 1;
      while (j < n) {
        if (operation[j] == '"') {
          if (j + 1 < n && operation[j + 1] == '"') {
            j += 2;
            continue;
          }
          ++j;
          break;
        }
        ++j;
      }
      result.append(operation, i, j - i);
      i = j;
      continue;
    }

    // Dollar-quoted string: $tag$ ... $tag$. Only a token start opens one; a $ inside an unquoted
    // identifier (`a$$`, `a$b$`) is part of that identifier.
    if (c == '$' && at_token_start(i)) {
      const size_t delim_end = dollar_delim_end(i);
      if (delim_end != std::string::npos) {
        const size_t end = scan_dollar(i, delim_end);
        result.append(operation, i, end - i);
        i = end;
        continue;
      }
    }

    // Escape string E'...' / e'...' as a standalone token (the E/e must not be part of a longer
    // identifier). Copied verbatim here; a password value in this form is handled below.
    if ((c == 'e' || c == 'E') && i + 1 < n && operation[i + 1] == '\'') {
      if (at_token_start(i)) {
        const size_t end = scan_string(i + 1, /* escape */ true);
        result.append(operation, i, end - i);
        i = end;
        continue;
      }
    }

    // Plain string literal '...': not a password value (those are consumed below), copy verbatim.
    if (c == '\'') {
      const size_t end = scan_string(i, /* escape */ false);
      result.append(operation, i, end - i);
      i = end;
      continue;
    }

    // Password clause at top level: `password` (case-insensitive; matching as a suffix is fine,
    // so HASHED PASSWORD is covered too) followed by optional whitespace, '=', optional
    // whitespace, and a string-constant value in any form (see sconst_end), which is replaced with
    // the placeholder. Redact *every* occurrence: a rejected statement can carry two password
    // clauses, and stopping after the first would leave the rest in cleartext.
    if (lower(c) == kKeyword[0] && i + kKeyword.size() <= n) {
      bool keyword = true;
      for (size_t k = 0; k < kKeyword.size(); ++k) {
        if (lower(operation[i + k]) != kKeyword[k]) {
          keyword = false;
          break;
        }
      }
      if (keyword) {
        size_t j = i + kKeyword.size();
        while (j < n && std::isspace(static_cast<unsigned char>(operation[j]))) {
          ++j;
        }
        if (j < n && operation[j] == '=') {
          ++j;
          while (j < n && std::isspace(static_cast<unsigned char>(operation[j]))) {
            ++j;
          }
          const size_t end = sconst_end(j);
          if (end != std::string::npos) {
            result.append(operation, i, j - i);  // `password` + ws + '=' + ws, kept verbatim.
            result.append(kRedactedPlaceholder);
            redacted = true;
            if (ranges) {
              ranges->push_back(RedactedRange{j, end});
            }
            if (end == n) {
              // Unterminated value (malformed or truncated): all that remains is password material.
              return result;
            }
            i = end;
            continue;
          }
        }
        // `password` not followed by `= '<literal>'`: emit the keyword and move on.
        result.append(operation, i, kKeyword.size());
        i += kKeyword.size();
        continue;
      }
    }

    result.push_back(c);
    ++i;
  }

  return redacted ? result : operation;
}

}  // namespace ql
}  // namespace yb
