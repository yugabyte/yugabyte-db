%top{
/*
 * The rules in this scanner implementation are based on the followings.
 *
 *     - openCypher
 *           - Cypher Query Language Reference (Version 9)
 *           - Grammar Specification (M13)
 *           - ANTLR Grammar (M13)
 *     - JSON (RFC 8259)
 */

#include "postgres.h"

#include "common/string.h"
#include "mb/pg_wchar.h"

#include "scan.h"
}

%option 8bit
%option never-interactive
%option noyywrap
%option reentrant
%option extra-type="struct ag_yy_extra"
%option prefix="ag_yy"
%option nounistd
%option fast noread
%option backup
%option perf-report perf-report
%option nodefault
%option warn

/* to override the default memory management */
%option noyyalloc noyyrealloc noyyfree

/* remove warnings */
%option noinput nounput
/* remove unneeded routines */
%option noyy_scan_bytes noyy_scan_string
%option noyyget_leng noyyget_text
%option noyyget_lineno noyyset_lineno
%option noyyget_in noyyset_in noyyget_out noyyset_out
%option noyyget_lval noyyset_lval noyyget_lloc noyyset_lloc
%option noyyget_debug noyyset_debug

/*
 * whitespace rule in Cypher handles twenty-four characters out of the
 * twenty-five characters defined as whitespace characters, four extra control
 * characters (FS, GS, RS, and US), and Mongolian vowel separator in Unicode.
 *
 * Only six of them below have been considered as whitespace characters here.
 * This character set is a superset of whitespace characters in JSON.
 *
 *     [\t\n\v\f\r ]
 *         U+0009 CHARACTER TABULATION (HT, Horizontal Tab)
 *         U+000A LINE FEED (LF)
 *         U+000B LINE TABULATION (VT, Vertical Tab)
 *         U+000C FORM FEED (FF)
 *         U+000D CARRIAGE RETURN (CR)
 *         U+0020 SPACE
 *
 * The other characters are listed below for future reference. To handle them,
 * you may use the patterns that match UTF-8 encoded code points of them.
 *
 *     \xC2[\x85\xA0]
 *         U+0085 NEXT LINE (NEL) -- not in Cypher
 *         U+00A0 NO-BREAK SPACE
 *     \xE1\x9A\x80
 *         U+1680 OGHAM SPACE MARK
 *     \xE2\x80[\x80-\x8A\xA8\xA9\xAF]
 *         U+2000 EN QUAD
 *         U+2001 EM QUAD
 *         U+2002 EN SPACE
 *         U+2003 EM SPACE
 *         U+2004 THREE-PER-EM SPACE
 *         U+2005 FOUR-PER-EM SPACE
 *         U+2006 SIX-PER-EM SPACE
 *         U+2007 FIGURE SPACE
 *         U+2008 PUNCTUATION SPACE
 *         U+2009 THIN SPACE
 *         U+200A HAIR SPACE
 *         U+2028 LINE SEPARATOR
 *         U+2029 PARAGRAPH SEPARATOR
 *         U+202F NARROW NO-BREAK SPACE
 *     \xE2\x81\x9F
 *         U+205F MEDIUM MATHEMATICAL SPACE
 *     \xE3\x80\x80
 *         U+3000 IDEOGRAPHIC SPACE
 *
 *     [\x1C-\x1F]
 *         U+001C INFORMATION SEPARATOR FOUR (FS, File Separator)
 *         U+001D INFORMATION SEPARATOR THREE (GS, Group Separator)
 *         U+001E INFORMATION SEPARATOR TWO (RS, Record Separator)
 *         U+001F INFORMATION SEPARATOR ONE (US, Unit Separator)
 *
 *     \xE1\xA0\x8E
 *         U+180E MONGOLIAN VOWEL SEPARATOR -- not a whitespace anymore
 */
whitespace [\t\n\v\f\r ]+

/*
 * Comment rule for multi-line comment in Cypher does not match comments that
 * end with an odd number of "*"s before the closing sequence.
 * Therefore, the rule has been modified so that it can match such comments.
 */
%x mlcomment
mlcstart  "/*"
mlcchars  [^*]+|\*+
mlcstop    \*+\/
slcomment "//"[^\n\r]*

/*
 * For numbers, unary plus and minus are handled as operators later in Cypher
 * grammar although JSON numbers may be prefixed with an optional minus sign.
 *
 * JSON does not support octal and hexadecimal integer literals.
 */

digit    [0-9]
hexdigit [0-9A-Fa-f]

/*
 * digitseq pattern covers DecimalInteger and OctalInteger rules in Cypher.
 * Integer in JSON is represented in "0|[1-9][0-9]*" pattern that is covered by
 * digitseq pattern.
 */
digitseq {digit}+

/*
 * hexint pattern covers HexInteger rule in Cypher and also accepts "0X" prefix
 * for convenience.
 */
hexint     0[Xx]{hexdigit}+
hexintfail 0[Xx]

/*
 * decimal pattern covers RegularDecimalReal rule in Cypher and also accepts
 * "{digitseq}\." pattern (e.g. "1.") which RegularDecimalReal rule doesn't.
 * Decimal in JSON is represented in "(0|[1-9][0-9]*)\.[0-9]+" pattern that is
 * covered by decimal pattern.
 *
 * decimalfail pattern is for ranges (e.g. "0..1"). The action for the pattern
 * consumes digitseq and returns dot_dot back to the input stream so that
 * dot_dot can be matched next.
 */
decimal     {digitseq}\.{digit}*|\.{digitseq}
decimalfail {digitseq}\.\.

/*
 * decimalsci pattern covers ExponentDecimalReal rule in Cypher. It also
 * accepts coefficients in "{digitseq}\." pattern and explicit positive
 * exponents ("+") which ExponentDecimalReal rule doesn't.
 * Scientific notation in JSON is represented in
 * "(0|[1-9][0-9]*)(\.[0-9]+)?[Ee][+-]?[0-9]+" pattern that is covered by
 * decimalsci pattern.
 */
decimalsci      ({digitseq}|{decimal})[Ee][+-]?{digitseq}
decimalscifail1 ({digitseq}|{decimal})[Ee]
decimalscifail2 ({digitseq}|{decimal})[Ee][+-]

/*
 * These patterns cover StringLiteral rule in Cypher and JSON strings.
 * The escape sequence "\/" has been added for JSON strings.
 *
 * esasciifail and esunicodefail patterns handle escape sequences that are not
 * accepted by esascii and esunicode patterns respectively.
 *
 * Since esasciifail pattern can match anything that esascii pattern can,
 * esascii must appear first before esasciifail in the rules section.
 *
 * qstru start condition is for Unicode low surrogates.
 */
%x dqstr sqstr qstru
dquote        \"
dqchars       [^"\\]+
squote        '
sqchars       [^'\\]+
esascii       \\["'/\\bfnrt]
esasciifail   \\[^Uu]?
esunicode     \\(U{hexdigit}{8}|u{hexdigit}{4})
esunicodefail \\(U{hexdigit}{0,7}|u{hexdigit}{0,3})
any           (?s:.)

/* id pattern is for UnescapedSymbolicName rule in Cypher. */
id      {idstart}{idcont}*
idstart [A-Z_a-z\x80-\xFF]
idcont  [$0-9A-Z_a-z\x80-\xFF]

/* These are for EscapedSymbolicName rule in Cypher. */
%x bqid
bquote   `
bqchars  [^`]+
esbquote {bquote}{bquote}

/*
 * Parameter rule in Cypher is "$" followed by SymbolicName or DecimalInteger
 * rule. However, according to "Cypher Query Language Reference",
 *
 *     Parameters may consist of letters and numbers, and any combination of
 *     these, but cannot start with a number or a currency symbol.
 *
 * So, a modified version of Parameter rule that follows the above explanation
 * has been used.
 */
param \${id}

/*
 * These are tokens that are used as operators and language constructs in
 * Cypher, and some of them are structural characters in JSON.
 */
lt_gt    "<>"
lt_eq    "<="
gt_eq    ">="
dot_dot  ".."
plus_eq  "+="
eq_tilde "=~"
self     [%()*+,\-./:<=>[\]^{|}]

other .

%{
struct strbuf {
    char *buffer;
    int capacity;
    int length;
};

static void strbuf_init(struct strbuf *sb, int capacity);
static void strbuf_cleanup(struct strbuf *sb);
static void strbuf_append_buf(struct strbuf *sb, const char *b, const int len);
static void strbuf_append_char(struct strbuf *sb, const char c);
static void strbuf_append_codepoint(struct strbuf *sb, const pg_wchar c);
static void strbuf_ensure_capacity(struct strbuf *sb, int len);
static const char *strbuf_get_str(struct strbuf *sb);
static void strbuf_reset(struct strbuf *sb);

struct ag_yy_extra {
    // accumulate matched strings to build a complete literal if multiple rules
    // are needed to scan it
    struct strbuf literal_buf;

    // for Unicode surrogate pair
    pg_wchar high_surrogate;
    int start_cond;

    // for the location of the current token and the actual position of it
    const char *scan_buf;
    int last_loc;
};

static void integer_literal_to_token(const char *s, struct ag_token *token,
                                     const struct ag_yy_extra *extra);

static bool is_high_surrogate(const pg_wchar c);
static bool is_low_surrogate(const pg_wchar c);
static pg_wchar surrogate_pair_to_codepoint(const pg_wchar hi,
                                            const pg_wchar lo);

#define update_location() \
    do { \
        yyextra.last_loc = yytext - yyextra.scan_buf; \
    } while (0)
#define get_location() (yyextra.last_loc)

#define scan_errmsg(msg) _scan_errmsg(msg, &yyextra)
static int _scan_errmsg(const char *msg, const struct ag_yy_extra *extra);
#define scan_errposition() _scan_errposition(&yyextra)
static int _scan_errposition(const struct ag_yy_extra *extra);

// Avoid exit() on fatal scanner errors.
// Call yy_fatal_error() just to keep compiler quiet.
#define YY_FATAL_ERROR(msg) \
    do { \
        ereport(ERROR, (errmsg_internal("%s", msg))); \
        yy_fatal_error(NULL, NULL); \
    } while (0)

// "yyscanner" must be used for the name of the parameter because it is
// referenced internally. "yyscan_t" is OK because it is actually "void *"
// and is the same with "ag_scanner_t".
#define YY_DECL struct ag_token ag_scanner_next_token(yyscan_t yyscanner)
%}

%%

%{
// This is used in the actions below.
struct ag_token token;
%}

{whitespace} {
    // ignore
}

{mlcstart} {
    // update location in case of unterminated comment
    update_location();
    BEGIN(mlcomment);
}

<mlcomment>{mlcchars} {
    // ignore
}

<mlcomment>{mlcstop} {
    BEGIN(INITIAL);
}

<mlcomment><<EOF>> {
    ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                    scan_errmsg("unterminated /* comment"),
                    scan_errposition()));
}

{slcomment} {
    // ignore
}

{digitseq} |
{hexint} {
    update_location();
    integer_literal_to_token(yytext, &token, &yyextra);
    token.location = get_location();
    return token;
}

{hexintfail} {
    update_location();
    ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                    scan_errmsg("invalid hexadecimal integer literal"),
                    scan_errposition()));
}

{decimal} |
{decimalsci} {
    update_location();
    token.type = AG_TOKEN_DECIMAL;
    token.value.s = yytext;
    token.location = get_location();
    return token;
}

{decimalfail} {
    // return dot_dot back to the input stream
    yyless(yyleng - 2);

    update_location();

    // consume digitseq
    integer_literal_to_token(yytext, &token, &yyextra);
    token.location = get_location();
    return token;
}

{decimalscifail1} |
{decimalscifail2} {
    update_location();
    ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                    scan_errmsg("invalid scientific notation literal"),
                    scan_errposition()));
}

{dquote} {
    update_location();
    strbuf_reset(&yyextra.literal_buf);
    BEGIN(dqstr);
}

{squote} {
    update_location();
    strbuf_reset(&yyextra.literal_buf);
    BEGIN(sqstr);
}

<dqstr>{dqchars} |
<sqstr>{sqchars} {
    strbuf_append_buf(&yyextra.literal_buf, yytext, yyleng);
}

<dqstr,sqstr>{esascii} {
    char c;

    switch (yytext[1]) {
    case 'b':
        c = '\b';
        break;
    case 'f':
        c = '\f';
        break;
    case 'n':
        c = '\n';
        break;
    case 'r':
        c = '\r';
        break;
    case 't':
        c = '\t';
        break;
    default:
        // '"', '\'', '/', and '\\'
        c = yytext[1];
        break;
    }

    strbuf_append_char(&yyextra.literal_buf, c);
}

<dqstr,sqstr>{esasciifail} {
    if (yyleng == 1) {
        // This happens when the scanner meets "\"<<EOF>>. Just consume "\"
        // so that <dqstr,sqstr,qstru><<EOF>> rule can do the rest.
        strbuf_append_char(&yyextra.literal_buf, '\\');
    } else {
        update_location();
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
                 scan_errmsg("invalid escape sequence"),
                 errdetail("Valid escape sequences are \\\", \\', \\/, \\\\, \\b, \\f, \\n, \\r, \\t, \\uXXXX, and \\UXXXXXXXX."),
                 scan_errposition()));
    }
}

<dqstr,sqstr>{esunicode} {
    pg_wchar c;

    // It is unnecessary to check endptr and errno here.
    c = strtoul(yytext + 2, NULL, 16);
    if (c > 0x10FFFF) {
        // c is greater than the maximum value of a Unicode code point.
        update_location();
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
                 scan_errmsg("invalid Unicode escape value"),
                 errdetail("Unicode escape values cannot be greater than 10FFFF, which is the maximum value of a code point."),
                 scan_errposition()));
    } else if (c > 0x7F) {
        if (GetDatabaseEncoding() != PG_UTF8) {
            update_location();
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
                     scan_errmsg("unsupported Unicode escape value"),
                     errdetail("Unicode escape values cannot be used for code point values above 007F when the server encoding is not UTF8."),
                     scan_errposition()));
        }

        if (is_high_surrogate(c)) {
            yyextra.high_surrogate = c;
            yyextra.start_cond = YY_START;
            BEGIN(qstru);
        } else if (is_low_surrogate(c)) {
            update_location();
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
                     scan_errmsg("invalid Unicode surrogate pair"),
                     errdetail("A low surrogate must follow a high surrogate."),
                     scan_errposition()));
        } else {
            strbuf_append_codepoint(&yyextra.literal_buf, c);
        }
    } else if (c > 0) {
        // c is an ASCII character.
        strbuf_append_char(&yyextra.literal_buf, (char)c);
    } else {
        // U+0000 NUL is the minimum value of a Unicode code point.
        // However, it is invalid in quoted strings as well as query strings.
        update_location();
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
                 scan_errmsg("unsupported Unicode escape value"),
                 errdetail("Unicode code point value 0000 is not allowed in quoted strings."),
                 scan_errposition()));
    }
}

<qstru>{esunicode} {
    pg_wchar c;

    c = strtoul(yytext + 2, NULL, 16);
    if (is_low_surrogate(c)) {
        c = surrogate_pair_to_codepoint(yyextra.high_surrogate, c);
        // 0x010000 <= c <= 0x10FFFF always holds for surrogate pairs.
        strbuf_append_codepoint(&yyextra.literal_buf, c);
        BEGIN(yyextra.start_cond);
    } else {
        update_location();
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
                 scan_errmsg("invalid Unicode surrogate pair"),
                 errdetail("A low surrogate must follow a high surrogate."),
                 scan_errposition()));
    }
}

<dqstr,sqstr,qstru>{esunicodefail} {
    update_location();
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
             scan_errmsg("invalid Unicode escape sequence"),
             errhint("Unicode escape sequences must be \\uXXXX or \\UXXXXXXXX."),
             scan_errposition()));
}

<qstru>{any} {
    update_location();
    ereport(ERROR, (errcode(ERRCODE_INVALID_ESCAPE_SEQUENCE),
                    scan_errmsg("invalid Unicode surrogate pair"),
                    errdetail("A low surrogate must follow a high surrogate."),
                    scan_errposition()));
}

<dqstr>{dquote} |
<sqstr>{squote} {
    BEGIN(INITIAL);

    // In quoted strings, only Unicode escape sequences need to be verified,
    // and the actions for <dqstr,sqstr>{esunicode} and <qstru>{esunicode}
    // rules verify the code point values. So, quoted strings are always valid.

    token.type = AG_TOKEN_STRING;
    token.value.s = strbuf_get_str(&yyextra.literal_buf);
    token.location = get_location();
    return token;
}

<dqstr,sqstr,qstru><<EOF>> {
    ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                    scan_errmsg("unterminated quoted string"),
                    scan_errposition()));
}

{id} {
    update_location();
    token.type = AG_TOKEN_IDENTIFIER;
    token.value.s = yytext;
    token.location = get_location();
    return token;
}

{bquote} {
    update_location();
    strbuf_reset(&yyextra.literal_buf);
    BEGIN(bqid);
}

<bqid>{bqchars} {
    strbuf_append_buf(&yyextra.literal_buf, yytext, yyleng);
}

<bqid>{esbquote} {
    strbuf_append_char(&yyextra.literal_buf, '`');
}

<bqid>{bquote} {
    BEGIN(INITIAL);

    if (yyextra.literal_buf.length == 0) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_NAME),
                        scan_errmsg("zero-length quoted identifier"),
                        scan_errposition()));
    }

    token.type = AG_TOKEN_IDENTIFIER;
    token.value.s = strbuf_get_str(&yyextra.literal_buf);
    token.location = get_location();
    return token;
}

<bqid><<EOF>> {
    ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                    scan_errmsg("unterminated quoted identifier"),
                    scan_errposition()));
}

{param} {
    update_location();
    token.type = AG_TOKEN_PARAMETER;
    token.value.s = yytext + 1;
    token.location = get_location();
    return token;
}

{lt_gt} {
    update_location();
    token.type = AG_TOKEN_LT_GT;
    token.value.s = yytext;
    token.location = get_location();
    return token;
}

{lt_eq} {
    update_location();
    token.type = AG_TOKEN_LT_EQ;
    token.value.s = yytext;
    token.location = get_location();
    return token;
}

{gt_eq} {
    update_location();
    token.type = AG_TOKEN_GT_EQ;
    token.value.s = yytext;
    token.location = get_location();
    return token;
}

{dot_dot} {
    update_location();
    token.type = AG_TOKEN_DOT_DOT;
    token.value.s = yytext;
    token.location = get_location();
    return token;
}

{plus_eq} {
    update_location();
    token.type = AG_TOKEN_PLUS_EQ;
    token.value.s = yytext;
    token.location = get_location();
    return token;
}

{eq_tilde} {
    update_location();
    token.type = AG_TOKEN_EQ_TILDE;
    token.value.s = yytext;
    token.location = get_location();
    return token;
}

{self} {
    update_location();
    token.type = AG_TOKEN_CHAR;
    token.value.c = yytext[0];
    token.location = get_location();
    return token;
}

{other} {
    update_location();
    ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                    scan_errmsg("unexpected character"),
                    scan_errposition()));
}

<<EOF>> {
    update_location();
    token.type = AG_TOKEN_NULL;
    token.value.c = '\0';
    token.location = get_location();
    return token;
}

%%

// Override the default memory management to make flex use palloc() instead of
// malloc().

void *ag_yyalloc(yy_size_t size, yyscan_t yyscanner)
{
    return palloc(size);
}

void *ag_yyrealloc(void *ptr, yy_size_t size, yyscan_t yyscanner)
{
    // see realloc(3)
    if (ptr) {
        if (size == 0) {
            pfree(ptr);
            return NULL;
        } else {
            return repalloc(ptr, size);
        }
    } else {
        return palloc(size);
    }
}

void ag_yyfree(void *ptr, yyscan_t yyscanner)
{
    if (ptr)
        pfree(ptr);
}

static void strbuf_init(struct strbuf *sb, int capacity)
{
    sb->buffer = palloc(capacity);
    sb->capacity = capacity;
    sb->length = 0;
}

static void strbuf_cleanup(struct strbuf *sb)
{
    if (sb->buffer)
        pfree(sb->buffer);
}

static void strbuf_append_buf(struct strbuf *sb, const char *b, const int len)
{
    strbuf_ensure_capacity(sb, sb->length + len);
    memcpy(sb->buffer + sb->length, b, len);
    sb->length += len;
}

static void strbuf_append_char(struct strbuf *sb, const char c)
{
    strbuf_ensure_capacity(sb, sb->length + 1);
    sb->buffer[sb->length] = c;
    sb->length += 1;
}

static void strbuf_append_codepoint(struct strbuf *sb, const pg_wchar c)
{
    unsigned char buf[6];

    unicode_to_utf8(c, buf);
    strbuf_append_buf(sb, (char *)buf, pg_utf_mblen(buf));
}

// len cannot be greater than MaxAllocSize because ReadCommand() reads
// a message and places the message body in StringInfo.
static void strbuf_ensure_capacity(struct strbuf *sb, int len)
{
    // consider additional 1 byte for the last '\0' character
    if (len < sb->capacity)
        return;

    do {
        sb->capacity *= 2;
    } while (sb->capacity <= len);

    sb->buffer = repalloc(sb->buffer, sb->capacity);
}

static const char *strbuf_get_str(struct strbuf *sb)
{
    sb->buffer[sb->length] = '\0';
    return sb->buffer;
}

static void strbuf_reset(struct strbuf *sb)
{
    sb->length = 0;
}

static void integer_literal_to_token(const char *s, struct ag_token *token,
                                     const struct ag_yy_extra *extra)
{
    char *endptr;
    int i;

    errno = 0;
    i = strtoint(s, &endptr, 0);

    // This is only needed for invalid octal integer literals. (e.g. "08")
    // Other cases cannot happen because of digitseq and hexint rules.
    if (*endptr != '\0') {
        ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                        _scan_errmsg("invalid octal integer literal", extra),
                        _scan_errposition(extra)));
    }

    // Treat it as a decimal if it is too large to be an "int" value.
    if (errno == ERANGE) {
        token->type = AG_TOKEN_DECIMAL;
        token->value.s = s;
        return;
    }

    token->type = AG_TOKEN_INTEGER;
    token->value.i = i;
}

static bool is_high_surrogate(const pg_wchar c)
{
    return (c >= 0xD800 && c <= 0xDBFF);
}

static bool is_low_surrogate(const pg_wchar c)
{
    return (c >= 0xDC00 && c <= 0xDFFF);
}

// http://www.unicode.org/faq/utf_bom.html#utf16-4
static pg_wchar surrogate_pair_to_codepoint(const pg_wchar hi,
                                            const pg_wchar lo)
{
    const pg_wchar surrogate_offset = 0x10000 - (0xD800 << 10) - 0xDC00;

    return (hi << 10) + lo + surrogate_offset;
}

static int _scan_errmsg(const char *msg, const struct ag_yy_extra *extra)
{
    const char *t = extra->scan_buf + extra->last_loc;

    if (t[0] == YY_END_OF_BUFFER_CHAR)
        return errmsg("%s at end of input", msg);
    else
        return errmsg("%s at or near \"%s\"", msg, t);
}

static int _scan_errposition(const struct ag_yy_extra *extra)
{
    int pos;

    pos = pg_mbstrlen_with_len(extra->scan_buf, extra->last_loc) + 1;

    return errposition(pos);
}

ag_scanner_t ag_scanner_create(const char *s)
{
    Size len;
    char *buf;
    yyscan_t yyscanner;
    struct ag_yy_extra extra;
    int ret;

    // The last two YY_END_OF_BUFFER_CHAR are required by flex.
    len = strlen(s);
    buf = palloc(len + 2);
    memcpy(buf, s, len);
    buf[len] = YY_END_OF_BUFFER_CHAR;
    buf[len + 1] = YY_END_OF_BUFFER_CHAR;

    ret = ag_yylex_init(&yyscanner);
    if (ret)
        elog(ERROR, "ag_yylex_init() failed: %m");

    strbuf_init(&extra.literal_buf, 1024);
    extra.high_surrogate = 0;
    extra.start_cond = INITIAL;
    extra.scan_buf = buf;
    extra.last_loc = 0;
    ag_yyset_extra(extra, yyscanner);

    ag_yy_scan_buffer(buf, len + 2, yyscanner);

    return yyscanner;
}

void ag_scanner_destroy(ag_scanner_t scanner)
{
    struct ag_yy_extra extra;

    extra = ag_yyget_extra(scanner);
    strbuf_cleanup(&extra.literal_buf);

    ag_yylex_destroy(scanner);
}
