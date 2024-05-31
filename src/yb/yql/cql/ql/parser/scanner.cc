//--------------------------------------------------------------------------------------------------
// The following only applies to changes made to this file as part of YugaByte development.
//
// Copyright (c) YugaByte, Inc.
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
// Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
// Portions Copyright (c) 1994, Regents of the University of California
//
// API for the core scanner (flex machine). Some of the functions are not following Yugabyte naming
// convention because they are PostgreQL code.
//--------------------------------------------------------------------------------------------------

// #include <algorithm>
#include <unordered_map>

#include "yb/gutil/casts.h"

#include "yb/yql/cql/ql/parser/parser.h"
#include "yb/yql/cql/ql/parser/scanner.h"
#include "yb/yql/cql/ql/parser/scanner_util.h"
#include "yb/util/logging.h"

namespace yb {
namespace ql {

using std::unordered_map;
using std::string;

//--------------------------------------------------------------------------------------------------
// Class LexProcessor.
//--------------------------------------------------------------------------------------------------

LexProcessor::LexProcessor()
    : literalbuf_(nullptr),
      literallen_(0),
      literalalloc_(0),
      backslash_quote_(BackslashQuoteType::SAFE_ENCODING),
      escape_string_warning_(true),
      standard_conforming_strings_(true) {
}

LexProcessor::~LexProcessor() {
  if (literalbuf_ != nullptr) {
    free(literalbuf_);
  }
}

//--------------------------------------------------------------------------------------------------

void LexProcessor::ScanInit(ParseContext *parse_context) {
  yyrestart(parse_context->ql_file());

  token_loc_.initialize();
  cursor_.initialize();
  lookahead_.type = 0;

  literallen_ = 0;
  xcdepth_ = 0;
  dolqstart_ = nullptr;
  utf16_first_part_ = 0;
  warn_on_first_escape_ = false;
  saw_non_ascii_ = false;

  backslash_quote_ = BackslashQuoteType::SAFE_ENCODING;
  escape_string_warning_ = true;
  standard_conforming_strings_ = true;

  parse_context_ = parse_context;
  if (parse_context_ != nullptr) {
    yy_flex_debug = parse_context->trace_scanning();
  }
}

//--------------------------------------------------------------------------------------------------

GramProcessor::symbol_type LexProcessor::Scan() {
  // Use the lookahead from the context if it's available. Otherwise, read the token.
  GramProcessor::symbol_type cur_token;
  ScanState scan_state;

  if (lookahead_.token() != 0) {
    // Remove lookahead from the context and reset it to type 0.
    cur_token.move(lookahead_);
    lookahead_.type = 0;
  } else {
    // Read the next token and save it to 'cur_token'.
    ScanNextToken(scan_state, &cur_token);
  }

  // Return the token if it doesn't require lookahead. Otherwise, set the token length.
  switch (cur_token.token()) {
    case GramProcessor::token::TOK_GROUP_P:
    case GramProcessor::token::TOK_OFFSET:
    case GramProcessor::token::TOK_NOT:
    case GramProcessor::token::TOK_NULLS_P:
    case GramProcessor::token::TOK_WITH: {
      break;
    }

    default: {
      // Return 'cur_token' as it does not require lookahead.
      return cur_token;
    }
  }

  // Cache the lookahead token.
  ScanNextToken(scan_state, &lookahead_);

  // Replace cur_token if needed, based on lookahead.
  GramProcessor::token_type next_token_type = lookahead_.token();
  switch (cur_token.token()) {
    case GramProcessor::token::TOK_GROUP_P: {
      // Replace GROUP_P with GROUP_LA to support SELECT ... GROUP BY ...
      // - Token GROUP_P is accepted when being used as column name (practically all names).
      // - Token GROUP_LA is accepted when being used in GROUP BY clause.
      //   group_clause: GROUP_LA BY <group_by_list>
      int next_tok = static_cast<int>(next_token_type);
      if (next_tok == GramProcessor::token::TOK_BY) {
        return GramProcessor::make_GROUP_LA(cursor_);
      }
      break;
    }

    case GramProcessor::token::TOK_OFFSET: {
      // Replace OFFSET with OFFSET_LA to support SELECT ... OFFSET ...
      // - Token OFFSET is accepted when being used as column name (practically all names).
      // - Token OFFSET_LA is accepted when being used in OFFSET clause.
      //   offset_clause:  OFFSET_LA <int constant>
      //                   OFFSET_LA '?'  --> Bind variable
      //                   OFFSET_LA ':'  --> Bind variable
      int next_tok = static_cast<int>(next_token_type);
      if (next_tok == GramProcessor::token::TOK_ICONST || next_tok == '?' || next_tok == ':') {
        return GramProcessor::make_OFFSET_LA(cursor_);
      }
      break;
    }

    case GramProcessor::token::TOK_NOT: {
      // Replace NOT by NOT_LA if it's followed by BETWEEN, IN, etc.
      switch (next_token_type) {
        case GramProcessor::token::TOK_BETWEEN:
        case GramProcessor::token::TOK_EXISTS:
        case GramProcessor::token::TOK_IN_P:
        case GramProcessor::token::TOK_LIKE:
        case GramProcessor::token::TOK_ILIKE:
        case GramProcessor::token::TOK_SIMILAR: {
          return GramProcessor::make_NOT_LA(cursor_);
        }

        default: {
          break;
        }
      }
      break;
    }

    case GramProcessor::token::TOK_NULLS_P: {
      // Replace NULLS_P by NULLS_LA if it's followed by FIRST or LAST.
      switch (next_token_type) {
        case GramProcessor::token::TOK_FIRST_P:
        case GramProcessor::token::TOK_LAST_P: {
          return GramProcessor::make_NULLS_LA(cursor_);
        }

        default: {
          break;
        }
      }
      break;
    }

    case GramProcessor::token::TOK_WITH: {
      // Replace WITH by WITH_LA if it's followed by TIME or ORDINALITY.
      switch (next_token_type) {
        case GramProcessor::token::TOK_TIME:
        case GramProcessor::token::TOK_ORDINALITY: {
          return GramProcessor::make_WITH_LA(cursor_);
        }

        default: {
          break;
        }
      }
      break;
    }

    default: {
      break;
    }
  }

  return cur_token;
}

//--------------------------------------------------------------------------------------------------

int LexProcessor::LexerInput(char* buf, int max_size) {
  return narrow_cast<int>(parse_context_->Read(buf, max_size));
}

//--------------------------------------------------------------------------------------------------

void LexProcessor::CountNewlineInToken(const string& token) {
  const auto lines =
    count(token.begin(), token.end(), '\n') + count(token.begin(), token.end(), '\r');
  cursor_.lines(narrow_cast<int>(lines));
}

//--------------------------------------------------------------------------------------------------

GramProcessor::symbol_type LexProcessor::ScanError(const char *token) {
  // Flex scanner will raise exception by itself, so we don't return Status::Error here.
  Status s = parse_context_->Error(token_loc_,
                                   "Lexical error at or near ",
                                   ErrorCode::LEXICAL_ERROR,
                                   token);
  VLOG(3) << s.ToString();
  return GramProcessor::make_SCAN_ERROR(cursor_);
}

GramProcessor::symbol_type LexProcessor::ScanError(const char *message, ErrorCode errcode) {
  // Flex scanner will raise exception by itself, so we don't return Status::Error here.
  Status s = parse_context_->Error(token_loc_, message, errcode);
  VLOG(3) << s.ToString();
  return GramProcessor::make_SCAN_ERROR(cursor_);
}

//--------------------------------------------------------------------------------------------------

void LexProcessor::ScanNextToken(const ScanState& scan_state,
                                 GramProcessor::symbol_type *next_token) {
  GramProcessor::symbol_type new_token(yylex(scan_state));
  next_token->move(new_token);
}

//--------------------------------------------------------------------------------------------------

MCSharedPtr<MCString> LexProcessor::ScanLiteral() {
  // Convert the literal to string and count the newline character.
  MCSharedPtr<MCString> value = MCMakeShared<MCString>(PTreeMem(), literalbuf_, literallen_);
  // Count newlines in this literal.
  CountNewlineInToken(value->c_str());

  return value;
}

//--------------------------------------------------------------------------------------------------

MCSharedPtr<MCString> LexProcessor::MakeIdentifier(const char *text, int len, bool warn) {
  // SQL99 specifies Unicode-aware case normalization, which we don't yet
  // have the infrastructure for.  Instead we use tolower() to provide a
  // locale-aware translation.  However, there are some locales where this
  // is not right either (eg, Turkish may do strange things with 'i' and
  // 'I').  Our current compromise is to use tolower() for characters with
  // the high bit set, as long as they aren't part of a multi-byte
  // character, and use an ASCII-only downcasing for 7-bit characters.
  MCSharedPtr<MCString> ident = MCMakeShared<MCString>(PTreeMem(), len, '\0');
  int i;
  for (i = 0; i < len; i++) {
    unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch >= 'A' && ch <= 'Z') {
      ch += 'a' - 'A';
    }
    (*ident)[i] = static_cast<char>(ch);
  }

  if (i >= NAMEDATALEN) {
    TruncateIdentifier(ident, warn);
  }
  return ident;
}

void LexProcessor::TruncateIdentifier(const MCSharedPtr<MCString>& ident, bool warn) {
  auto len = ident->length();
  if (len >= NAMEDATALEN) {
    len = pg_encoding_mbcliplen(ident->c_str(), len, NAMEDATALEN - 1);
    if (warn) {
      // We avoid using %.*s here because it can misbehave if the data
      // is not valid in what libc thinks is the prevailing encoding.
      char buf[NAMEDATALEN];
      memcpy(buf, ident->c_str(), len);
      buf[len] = '\0';
      char warn_msg[1024];
      snprintf(warn_msg, sizeof(warn_msg),
               "Identifier %s will be truncated to %s", ident->c_str(), buf);
      parse_context_->Warn(token_loc_, warn_msg, ErrorCode::NAME_TOO_LONG);
    }
    ident->resize(len);
  }
}

//--------------------------------------------------------------------------------------------------
// NOTE: All entities below this line in this modules are copies of PostgreQL's code. We made some
// minor changes to avoid lint errors such as using '{' for if blocks and change the comment style
// from '/**/' to '//'.
//--------------------------------------------------------------------------------------------------

inline void LexProcessor::EnlargeLiteralBuf(size_t bytes) {
  // Increase literalbuf by the given number of "bytes".
  auto prev_literalalloc = literalalloc_;
  if (prev_literalalloc == 0) {
    literalalloc_ = 4096;
  }
  while (literalalloc_ < literallen_ + bytes) {
    literalalloc_ <<= 1;
  }
  if (prev_literalalloc != literalalloc_) {
    literalbuf_ = reinterpret_cast<char *>(realloc(literalbuf_, literalalloc_));
  }
}

void LexProcessor::startlit() {
  literallen_ = 0;
}

void LexProcessor::addlit(char *ytext, size_t yleng) {
  // Enlarge buffer if needed.
  EnlargeLiteralBuf(yleng);

  // Append new data.
  memcpy(literalbuf_ + literallen_, ytext, yleng);
  literallen_ += yleng;
}

void LexProcessor::addlitchar(unsigned char ychar) {
  // Enlarge buffer if needed.
  EnlargeLiteralBuf(1);

  // Append new data.
  literalbuf_[literallen_++] = ychar;
}

char *LexProcessor::litbuf_udeescape(unsigned char escape) {
  char     *new_litbuf;
  char     *litbuf, *in, *out;
  pg_wchar  pair_first = 0;

  // Make literalbuf null-terminated to simplify the scanning loop.
  litbuf = literalbuf_;
  litbuf[literallen_] = '\0';

  // This relies on the subtle assumption that a UTF-8 expansion
  // cannot be longer than its escaped representation.
  new_litbuf = static_cast<char *>(PTempMem()->AllocateBytes(literallen_ + 1));

  in = litbuf;
  out = new_litbuf;
  while (*in) {
    if (in[0] == escape) {
      if (in[1] == escape) {
        if (pair_first) {
          AdvanceCursor(narrow_cast<int>(in - litbuf + 3)); // 3 for U&".
          ScanError("invalid Unicode surrogate pair");
        }
        *out++ = escape;
        in += 2;

      } else if (isxdigit((unsigned char) in[1]) &&
                 isxdigit((unsigned char) in[2]) &&
                 isxdigit((unsigned char) in[3]) &&
                 isxdigit((unsigned char) in[4])) {
        pg_wchar unicode;
        unicode = ((hexval(in[1]) << 12) +
                   (hexval(in[2]) << 8) +
                   (hexval(in[3]) << 4) +
                   hexval(in[4]));
        check_unicode_value(unicode, in);

        if (pair_first) {
          if (is_utf16_surrogate_second(unicode)) {
            unicode = surrogate_pair_to_codepoint(pair_first, unicode);
            pair_first = 0;
          } else {
            AdvanceCursor(narrow_cast<int>(in - litbuf + 3));   /* 3 for U&" */
            ScanError("invalid Unicode surrogate pair");
          }
        } else if (is_utf16_surrogate_second(unicode)) {
          ScanError("invalid Unicode surrogate pair");
        }

        if (is_utf16_surrogate_first(unicode)) {
          pair_first = unicode;
        } else {
          unicode_to_utf8(unicode, (unsigned char *) out);
          out += pg_utf_mblen((unsigned char *)out);
        }
        in += 5;

      } else if (in[1] == '+' &&
                 isxdigit((unsigned char) in[2]) &&
                 isxdigit((unsigned char) in[3]) &&
                 isxdigit((unsigned char) in[4]) &&
                 isxdigit((unsigned char) in[5]) &&
                 isxdigit((unsigned char) in[6]) &&
                 isxdigit((unsigned char) in[7])) {
        pg_wchar unicode;
        unicode = ((hexval(in[2]) << 20) +
                   (hexval(in[3]) << 16) +
                   (hexval(in[4]) << 12) +
                   (hexval(in[5]) << 8) +
                   (hexval(in[6]) << 4) +
                   hexval(in[7]));
        check_unicode_value(unicode, in);

        if (pair_first) {
          if (is_utf16_surrogate_second(unicode)) {
            unicode = surrogate_pair_to_codepoint(pair_first, unicode);
            pair_first = 0;
          } else {
            AdvanceCursor(narrow_cast<int>(in - litbuf + 3));   /* 3 for U&" */
            ScanError("invalid Unicode surrogate pair");
          }
        } else if (is_utf16_surrogate_second(unicode)) {
          ScanError("invalid Unicode surrogate pair");
        }

        if (is_utf16_surrogate_first(unicode)) {
          pair_first = unicode;
        } else {
          unicode_to_utf8(unicode, (unsigned char *) out);
          out += pg_utf_mblen((unsigned char *)out);
        }
        in += 8;
      } else {
        AdvanceCursor(narrow_cast<int>(in - litbuf + 3));   /* 3 for U&" */
        ScanError("invalid Unicode escape value");
      }
    } else {
      if (pair_first) {
        AdvanceCursor(narrow_cast<int>(in - litbuf + 3));   /* 3 for U&" */
        ScanError("invalid Unicode surrogate pair");
      }
      *out++ = *in++;
    }
  }
  *out = '\0';

  // We could skip pg_verifymbstr if we didn't process any non-7-bit-ASCII
  // codes; but it's probably not worth the trouble, since this isn't
  // likely to be a performance-critical path.
  pg_verify_mbstr_len(new_litbuf, out - new_litbuf, false);
  return new_litbuf;
}

//--------------------------------------------------------------------------------------------------

void LexProcessor::check_string_escape_warning(unsigned char ychar) {
  if (ychar == '\'') {
    if (warn_on_first_escape_ && escape_string_warning_)
      parse_context_->Warn(token_loc_,
                           "Nonstandard use of \\' in a string literal. Use '' to write quotes in "
                           "strings, or use the escape string syntax (E'...').",
                           ErrorCode::NONSTANDARD_USE_OF_ESCAPE_CHARACTER);
    warn_on_first_escape_ = false;  // Warn only once per string.
  } else if (ychar == '\\') {
    if (warn_on_first_escape_ && escape_string_warning_)
      parse_context_->Warn(token_loc_,
                           "(Nonstandard use of \\\\ in a string literal. Use the escape string "
                           "syntax for backslashes, e.g., E'\\\\'.",
                           ErrorCode::NONSTANDARD_USE_OF_ESCAPE_CHARACTER);
    warn_on_first_escape_ = false;  // Warn only once per string.
  } else {
    check_escape_warning();
  }
}

void LexProcessor::check_escape_warning() {
  if (warn_on_first_escape_ && escape_string_warning_)
    parse_context_->Warn(token_loc_,
                         "Nonstandard use of escape in a string literal. Use the escape string "
                         "syntax for escapes, e.g., E'\\r\\n'.",
                         ErrorCode::NONSTANDARD_USE_OF_ESCAPE_CHARACTER);
  warn_on_first_escape_ = false;  // Warn only once per string.
}

unsigned char LexProcessor::unescape_single_char(unsigned char c) {
  switch (c) {
    case 'b':
      return '\b';
    case 'f':
      return '\f';
    case 'n':
      return '\n';
    case 'r':
      return '\r';
    case 't':
      return '\t';
    default:
      /* check for backslash followed by non-7-bit-ASCII */
      if (c == '\0' || is_utf_highbit_set(c)) {
        saw_non_ascii_ = true;
      }
      return c;
  }
}

//--------------------------------------------------------------------------------------------------

void LexProcessor::addunicode(pg_wchar c) {
  char buf[8];

  if (c == 0 || c > 0x10FFFF)
    ScanError("invalid Unicode escape value");
  if (c > 0x7F) {
    saw_non_ascii_ = true;
  }
  unicode_to_utf8(c, (unsigned char *)buf);
  addlit(buf, pg_utf_mblen((unsigned char *)buf));
}

//--------------------------------------------------------------------------------------------------

static const ScanKeyword& kInvalidKeyword {
  "", GramProcessor::token::TOK_NULL_P, ScanKeyword::KeywordCategory::INVALID_KEYWORD
};

#define PG_KEYWORD(a, b, c) \
  {a, {a, GramProcessor::token::TOK_##b, ScanKeyword::KeywordCategory::c}},
const unordered_map<string, const ScanKeyword> kScanKeywords {
#include "yb/yql/cql/ql/kwlist.h"
};

const ScanKeyword& LexProcessor::ScanKeywordLookup(const char *text) {
  static const int kMaxKeywordBytes = 4096;
  char word[kMaxKeywordBytes];
  size_t word_bytes = strlen(text);

  // PostgreQL Note: Apply an ASCII-only downcasing.  We must not use tolower() since it may
  // produce the wrong translation in some locales (eg, Turkish).
  for (size_t i = 0; i < word_bytes; i++) {
    char ch = text[i];
    if (ch >= 'A' && ch <= 'Z') {
      ch += 'a' - 'A';
    }
    word[i] = ch;
  }
  word[word_bytes] = '\0';

  unordered_map<string, const ScanKeyword>::const_iterator iter = kScanKeywords.find(word);
  if (iter != kScanKeywords.end()) {
    return iter->second;
  }
  return kInvalidKeyword;
}

//--------------------------------------------------------------------------------------------------
// Class ScanStatus - Not yet implemented.
//--------------------------------------------------------------------------------------------------
ScanState::ScanState() {
}

ScanState::~ScanState() {
}

}  // namespace ql
}  // namespace yb
