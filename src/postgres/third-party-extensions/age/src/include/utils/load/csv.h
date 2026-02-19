/*
 * Created by Shoaib on 12/5/2021.
*/

/*
libcsv - parse and write csv data
Copyright (C) 2008-2021  Robert Gamble
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

#ifndef LIBCSV_H__
#define LIBCSV_H__
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CSV_MAJOR 3
#define CSV_MINOR 0
#define CSV_RELEASE 3

/* Error Codes */
#define CSV_SUCCESS 0
#define CSV_EPARSE 1   /* Parse error in strict mode */
#define CSV_ENOMEM 2   /* Out of memory while increasing buffer size */
#define CSV_ETOOBIG 3  /* Buffer larger than SIZE_MAX needed */
#define CSV_EINVALID 4 /* Invalid code,should never be received from csv_error*/


/* parser options */
#define CSV_STRICT 1    /* enable strict mode */
#define CSV_REPALL_NL 2 /* report all unquoted carriage returns and linefeeds */
#define CSV_STRICT_FINI 4 /* causes csv_fini to return CSV_EPARSE if last
                             field is quoted and doesn't contain ending
                             quote */
#define CSV_APPEND_NULL 8 /* Ensure that all fields are null-terminated */
#define CSV_EMPTY_IS_NULL 16 /* Pass null pointer to cb1 function when
                                empty, unquoted fields are encountered */


/* Character values */
#define CSV_TAB    0x09
#define CSV_SPACE  0x20
#define CSV_CR     0x0d
#define CSV_LF     0x0a
#define CSV_COMMA  0x2c
#define CSV_QUOTE  0x22

struct csv_parser {
    int pstate;         /* Parser state */
    int quoted;         /* Is the current field a quoted field? */
    size_t spaces;      /* Number of continuous spaces after quote or in a non-quoted field */
    unsigned char * entry_buf;   /* Entry buffer */
    size_t entry_pos;   /* Current position in entry_buf (and current size of entry) */
    size_t entry_size;  /* Size of entry buffer */
    int status;         /* Operation status */
    unsigned char options;
    unsigned char quote_char;
    unsigned char delim_char;
    int (*is_space)(unsigned char);
    int (*is_term)(unsigned char);
    size_t blk_size;
    void *(*malloc_func)(size_t);           /* not used */
    void *(*realloc_func)(void *, size_t);  /* function used to allocate buffer memory */
    void (*free_func)(void *);              /* function used to free buffer memory */
};

/* Function Prototypes */
int csv_init(struct csv_parser *p, unsigned char options);
int csv_fini(struct csv_parser *p, void (*cb1)(void *, size_t, void *), void (*cb2)(int, void *), void *data);
void csv_free(struct csv_parser *p);
int csv_error(const struct csv_parser *p);
const char * csv_strerror(int error);
size_t csv_parse(struct csv_parser *p, const void *s, size_t len, void (*cb1)(void *, size_t, void *), void (*cb2)(int, void *), void *data);
size_t csv_write(void *dest, size_t dest_size, const void *src, size_t src_size);
int csv_fwrite(FILE *fp, const void *src, size_t src_size);
size_t csv_write2(void *dest, size_t dest_size, const void *src, size_t src_size, unsigned char quote);
int csv_fwrite2(FILE *fp, const void *src, size_t src_size, unsigned char quote);
int csv_get_opts(const struct csv_parser *p);
int csv_set_opts(struct csv_parser *p, unsigned char options);
void csv_set_delim(struct csv_parser *p, unsigned char c);
void csv_set_quote(struct csv_parser *p, unsigned char c);
unsigned char csv_get_delim(const struct csv_parser *p);
unsigned char csv_get_quote(const struct csv_parser *p);
void csv_set_space_func(struct csv_parser *p, int (*f)(unsigned char));
void csv_set_term_func(struct csv_parser *p, int (*f)(unsigned char));
void csv_set_realloc_func(struct csv_parser *p, void *(*)(void *, size_t));
void csv_set_free_func(struct csv_parser *p, void (*)(void *));
void csv_set_blk_size(struct csv_parser *p, size_t);
size_t csv_get_buffer_size(const struct csv_parser *p);

#ifdef __cplusplus
}
#endif

#endif
