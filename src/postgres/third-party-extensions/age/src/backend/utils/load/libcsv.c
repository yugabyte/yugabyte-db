/*
libcsv - parse and write csv data
Copyright (C) 2008  Robert Gamble

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <assert.h>

#if __STDC_VERSION__ >= 199901L
#  include <stdint.h>
#else
 /* C89 doesn't have stdint.h or SIZE_MAX */
#  define SIZE_MAX ((size_t)-1)
#endif

#include "utils/load/csv.h"

#define VERSION "3.0.3"

#define ROW_NOT_BEGUN           0
#define FIELD_NOT_BEGUN         1
#define FIELD_BEGUN             2
#define FIELD_MIGHT_HAVE_ENDED  3

/*
  Explanation of states
  ROW_NOT_BEGUN    There have not been any fields encountered for this row
  FIELD_NOT_BEGUN  There have been fields but we are currently not in one
  FIELD_BEGUN      We are in a field
  FIELD_MIGHT_HAVE_ENDED
                   We encountered a double quote inside a quoted field, the
                   field is either ended or the quote is literal
*/

#define MEM_BLK_SIZE 128

#define SUBMIT_FIELD(p) \
  do { \
   if (!quoted) \
     entry_pos -= spaces; \
   if (p->options & CSV_APPEND_NULL) \
     ((p)->entry_buf[entry_pos]) = '\0'; \
   if (cb1 && (p->options & CSV_EMPTY_IS_NULL) && !quoted && entry_pos == 0) \
     cb1(NULL, entry_pos, data); \
   else if (cb1) \
     cb1(p->entry_buf, entry_pos, data); \
   pstate = FIELD_NOT_BEGUN; \
   entry_pos = quoted = spaces = 0; \
 } while (0)

#define SUBMIT_ROW(p, c) \
  do { \
    if (cb2) \
      cb2(c, data); \
    pstate = ROW_NOT_BEGUN; \
    entry_pos = quoted = spaces = 0; \
  } while (0)

#define SUBMIT_CHAR(p, c) ((p)->entry_buf[entry_pos++] = (c))

static const char *csv_errors[] = {"success",
                             "error parsing data while strict checking enabled",
                             "memory exhausted while increasing buffer size",
                             "data size too large",
                             "invalid status code"};

int
csv_error(const struct csv_parser *p)
{
  assert(p && "received null csv_parser");

  /* Return the current status of the parser */
  return p->status;
}

const char *
csv_strerror(int status)
{
  /* Return a textual description of status */
  if (status >= CSV_EINVALID || status < 0)
    return csv_errors[CSV_EINVALID];
  else
    return csv_errors[status];
}

int
csv_get_opts(const struct csv_parser *p)
{
  /* Return the currently set options of parser */
  if (p == NULL)
    return -1;

  return p->options;
}

int
csv_set_opts(struct csv_parser *p, unsigned char options)
{
  /* Set the options */
  if (p == NULL)
    return -1;

  p->options = options;
  return 0;
}

int
csv_init(struct csv_parser *p, unsigned char options)
{
  /* Initialize a csv_parser object returns 0 on success, -1 on error */
  if (p == NULL)
    return -1;

  p->entry_buf = NULL;
  p->pstate = ROW_NOT_BEGUN;
  p->quoted = 0;
  p->spaces = 0;
  p->entry_pos = 0;
  p->entry_size = 0;
  p->status = 0;
  p->options = options;
  p->quote_char = CSV_QUOTE;
  p->delim_char = CSV_COMMA;
  p->is_space = NULL;
  p->is_term = NULL;
  p->blk_size = MEM_BLK_SIZE;
  p->malloc_func = NULL;
  p->realloc_func = realloc;
  p->free_func = free;

  return 0;
}

void
csv_free(struct csv_parser *p)
{
  /* Free the entry_buffer of csv_parser object */
  if (p == NULL)
    return;

  if (p->entry_buf && p->free_func)
    p->free_func(p->entry_buf);

  p->entry_buf = NULL;
  p->entry_size = 0;

  return;
}

int
csv_fini(struct csv_parser *p, void (*cb1)(void *, size_t, void *), void (*cb2)(int c, void *), void *data)
{
  int quoted;
  int pstate;
  size_t spaces;
  size_t entry_pos;

  if (p == NULL)
    return -1;

  /* Finalize parsing.  Needed, for example, when file does not end in a newline */
  quoted = p->quoted;
  pstate = p->pstate;
  spaces = p->spaces;
  entry_pos = p->entry_pos;

  if ((pstate == FIELD_BEGUN) && p->quoted && (p->options & CSV_STRICT) && (p->options & CSV_STRICT_FINI)) {
    /* Current field is quoted, no end-quote was seen, and CSV_STRICT_FINI is set */
    p->status = CSV_EPARSE;
    return -1;
  }

  switch (pstate) {
    case FIELD_MIGHT_HAVE_ENDED:
      p->entry_pos -= p->spaces + 1;  /* get rid of spaces and original quote */
      entry_pos = p->entry_pos;
      /*lint -fallthrough */
    case FIELD_NOT_BEGUN:
    case FIELD_BEGUN:
      /* Unnecessary:
      quoted = p->quoted, pstate = p->pstate;
      spaces = p->spaces, entry_pos = p->entry_pos;
      */
      SUBMIT_FIELD(p);
      SUBMIT_ROW(p, -1);
      break;
    case ROW_NOT_BEGUN: /* Already ended properly */
      ;
  }

  /* Reset parser */
  p->spaces = p->quoted = p->entry_pos = p->status = 0;
  p->pstate = ROW_NOT_BEGUN;

  return 0;
}

void
csv_set_delim(struct csv_parser *p, unsigned char c)
{
  /* Set the delimiter */
  if (p) p->delim_char = c;
}

void
csv_set_quote(struct csv_parser *p, unsigned char c)
{
  /* Set the quote character */
  if (p) p->quote_char = c;
}

unsigned char
csv_get_delim(const struct csv_parser *p)
{
  assert(p && "received null csv_parser");

  /* Get the delimiter */
  return p->delim_char;
}

unsigned char
csv_get_quote(const struct csv_parser *p)
{
  assert(p && "received null csv_parser");

  /* Get the quote character */
  return p->quote_char;
}

void
csv_set_space_func(struct csv_parser *p, int (*f)(unsigned char))
{
  /* Set the space function */
  if (p) p->is_space = f;
}

void
csv_set_term_func(struct csv_parser *p, int (*f)(unsigned char))
{
  /* Set the term function */
  if (p) p->is_term = f;
}

void
csv_set_realloc_func(struct csv_parser *p, void *(*f)(void *, size_t))
{
  /* Set the realloc function used to increase buffer size */
  if (p && f) p->realloc_func = f;
}

void
csv_set_free_func(struct csv_parser *p, void (*f)(void *))
{
  /* Set the free function used to free the buffer */
  if (p && f) p->free_func = f;
}

void
csv_set_blk_size(struct csv_parser *p, size_t size)
{
  /* Set the block size used to increment buffer size */
  if (p) p->blk_size = size;
}

size_t
csv_get_buffer_size(const struct csv_parser *p)
{
  /* Get the size of the entry buffer */
  if (p)
    return p->entry_size;
  return 0;
}

static int
csv_increase_buffer(struct csv_parser *p)
{
  size_t to_add;
  void *vp;

  if (p == NULL) return 0;
  if (p->realloc_func == NULL) return 0;

  /* Increase the size of the entry buffer.  Attempt to increase size by
   * p->blk_size, if this is larger than SIZE_MAX try to increase current
   * buffer size to SIZE_MAX.  If allocation fails, try to allocate halve
   * the size and try again until successful or increment size is zero.
   */

  to_add = p->blk_size;

  if ( p->entry_size >= SIZE_MAX - to_add )
    to_add = SIZE_MAX - p->entry_size;

  if (!to_add) {
    p->status = CSV_ETOOBIG;
    return -1;
  }

  while ((vp = p->realloc_func(p->entry_buf, p->entry_size + to_add)) == NULL) {
    to_add /= 2;
    if (!to_add) {
      p->status = CSV_ENOMEM;
      return -1;
    }
  }

  /* Update entry buffer pointer and entry_size if successful */
  p->entry_buf = vp;
  p->entry_size += to_add;
  return 0;
}

size_t
csv_parse(struct csv_parser *p, const void *s, size_t len, void (*cb1)(void *, size_t, void *), void (*cb2)(int c, void *), void *data)
{
  unsigned const char *us = s;  /* Access input data as array of unsigned char */
  unsigned char c;              /* The character we are currently processing */
  size_t pos = 0;               /* The number of characters we have processed in this call */

  /* Store key fields into local variables for performance */
  unsigned char delim = p->delim_char;
  unsigned char quote = p->quote_char;
  int (*is_space)(unsigned char) = p->is_space;
  int (*is_term)(unsigned char) = p->is_term;
  int quoted = p->quoted;
  int pstate = p->pstate;
  size_t spaces = p->spaces;
  size_t entry_pos = p->entry_pos;


  if (!p->entry_buf && pos < len) {
    /* Buffer hasn't been allocated yet and len > 0 */
    if (csv_increase_buffer(p) != 0) {
      p->quoted = quoted, p->pstate = pstate, p->spaces = spaces, p->entry_pos = entry_pos;
      return pos;
    }
  }

  while (pos < len) {
    /* Check memory usage, increase buffer if necessary */
    if (entry_pos == ((p->options & CSV_APPEND_NULL) ? p->entry_size - 1 : p->entry_size) ) {
      if (csv_increase_buffer(p) != 0) {
        p->quoted = quoted, p->pstate = pstate, p->spaces = spaces, p->entry_pos = entry_pos;
        return pos;
      }
    }

    c = us[pos++];

    switch (pstate) {
      case ROW_NOT_BEGUN:
      case FIELD_NOT_BEGUN:
        if ((is_space ? is_space(c) : c == CSV_SPACE || c == CSV_TAB) && c!=delim) { /* Space or Tab */
          continue;
        } else if (is_term ? is_term(c) : c == CSV_CR || c == CSV_LF) { /* Carriage Return or Line Feed */
          if (pstate == FIELD_NOT_BEGUN) {
            SUBMIT_FIELD(p);
            SUBMIT_ROW(p, c);
          } else {  /* ROW_NOT_BEGUN */
            /* Don't submit empty rows by default */
            if (p->options & CSV_REPALL_NL) {
              SUBMIT_ROW(p, c);
            }
          }
          continue;
        } else if (c == delim) { /* Comma */
          SUBMIT_FIELD(p);
          break;
        } else if (c == quote) { /* Quote */
          pstate = FIELD_BEGUN;
          quoted = 1;
        } else {               /* Anything else */
          pstate = FIELD_BEGUN;
          quoted = 0;
          SUBMIT_CHAR(p, c);
        }
        break;
      case FIELD_BEGUN:
        if (c == quote) {         /* Quote */
          if (quoted) {
            SUBMIT_CHAR(p, c);
            pstate = FIELD_MIGHT_HAVE_ENDED;
          } else {
            /* STRICT ERROR - double quote inside non-quoted field */
            if (p->options & CSV_STRICT) {
              p->status = CSV_EPARSE;
              p->quoted = quoted, p->pstate = pstate, p->spaces = spaces, p->entry_pos = entry_pos;
              return pos-1;
            }
            SUBMIT_CHAR(p, c);
            spaces = 0;
          }
        } else if (c == delim) {  /* Comma */
          if (quoted) {
            SUBMIT_CHAR(p, c);
          } else {
            SUBMIT_FIELD(p);
          }
        } else if (is_term ? is_term(c) : c == CSV_CR || c == CSV_LF) {  /* Carriage Return or Line Feed */
          if (!quoted) {
            SUBMIT_FIELD(p);
            SUBMIT_ROW(p, c);
          } else {
            SUBMIT_CHAR(p, c);
          }
        } else if (!quoted && (is_space? is_space(c) : c == CSV_SPACE || c == CSV_TAB)) { /* Tab or space for non-quoted field */
            SUBMIT_CHAR(p, c);
            spaces++;
        } else {  /* Anything else */
          SUBMIT_CHAR(p, c);
          spaces = 0;
        }
        break;
      case FIELD_MIGHT_HAVE_ENDED:
        /* This only happens when a quote character is encountered in a quoted field */
        if (c == delim) {  /* Comma */
          entry_pos -= spaces + 1;  /* get rid of spaces and original quote */
          SUBMIT_FIELD(p);
        } else if (is_term ? is_term(c) : c == CSV_CR || c == CSV_LF) {  /* Carriage Return or Line Feed */
          entry_pos -= spaces + 1;  /* get rid of spaces and original quote */
          SUBMIT_FIELD(p);
          SUBMIT_ROW(p, c);
        } else if (is_space ? is_space(c) : c == CSV_SPACE || c == CSV_TAB) {  /* Space or Tab */
          SUBMIT_CHAR(p, c);
          spaces++;
        } else if (c == quote) {  /* Quote */
          if (spaces) {
            /* STRICT ERROR - unescaped double quote */
            if (p->options & CSV_STRICT) {
              p->status = CSV_EPARSE;
              p->quoted = quoted, p->pstate = pstate, p->spaces = spaces, p->entry_pos = entry_pos;
              return pos-1;
            }
            spaces = 0;
            SUBMIT_CHAR(p, c);
          } else {
            /* Two quotes in a row */
            pstate = FIELD_BEGUN;
          }
        } else {  /* Anything else */
          /* STRICT ERROR - unescaped double quote */
          if (p->options & CSV_STRICT) {
            p->status = CSV_EPARSE;
            p->quoted = quoted, p->pstate = pstate, p->spaces = spaces, p->entry_pos = entry_pos;
            return pos-1;
          }
          pstate = FIELD_BEGUN;
          spaces = 0;
          SUBMIT_CHAR(p, c);
        }
        break;
     default:
       break;
    }
  }
  p->quoted = quoted, p->pstate = pstate, p->spaces = spaces, p->entry_pos = entry_pos;
  return pos;
}

size_t
csv_write (void *dest, size_t dest_size, const void *src, size_t src_size)
{
  return csv_write2(dest, dest_size, src, src_size, CSV_QUOTE);
}

int
csv_fwrite (FILE *fp, const void *src, size_t src_size)
{
  return csv_fwrite2(fp, src, src_size, CSV_QUOTE);
}

size_t
csv_write2 (void *dest, size_t dest_size, const void *src, size_t src_size, unsigned char quote)
{
  unsigned char *cdest = dest;
  const unsigned char *csrc = src;
  size_t chars = 0;

  if (src == NULL)
    return 0;

  if (dest == NULL)
    dest_size = 0;

  if (dest_size > 0)
    *cdest++ = quote;
  chars++;

  while (src_size) {
    if (*csrc == quote) {
      if (dest_size > chars)
        *cdest++ = quote;
      if (chars < SIZE_MAX) chars++;
    }
    if (dest_size > chars)
      *cdest++ = *csrc;
    if (chars < SIZE_MAX) chars++;
    src_size--;
    csrc++;
  }

  if (dest_size > chars)
    *cdest = quote;
  if (chars < SIZE_MAX) chars++;

  return chars;
}

int
csv_fwrite2 (FILE *fp, const void *src, size_t src_size, unsigned char quote)
{
  const unsigned char *csrc = src;

  if (fp == NULL || src == NULL)
    return 0;

  if (fputc(quote, fp) == EOF)
    return EOF;

  while (src_size) {
    if (*csrc == quote) {
      if (fputc(quote, fp) == EOF)
        return EOF;
    }
    if (fputc(*csrc, fp) == EOF)
      return EOF;
    src_size--;
    csrc++;
  }

  if (fputc(quote, fp) == EOF) {
    return EOF;
  }

  return 0;
}
