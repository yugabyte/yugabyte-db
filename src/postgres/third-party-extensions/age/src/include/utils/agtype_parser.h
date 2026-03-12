/*
 * For PostgreSQL Database Management System:
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2010, The PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its documentation for any purpose,
 * without fee, and without a written agreement is hereby granted, provided that the above copyright notice
 * and this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY
 * OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA
 * HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

/*
 * Declarations for agtype parser.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 */

#ifndef AG_AGTYPE_PARSER_H
#define AG_AGTYPE_PARSER_H

typedef enum
{
    AGTYPE_TOKEN_INVALID,
    AGTYPE_TOKEN_STRING,
    AGTYPE_TOKEN_INTEGER,
    AGTYPE_TOKEN_FLOAT,
    AGTYPE_TOKEN_NUMERIC,
    AGTYPE_TOKEN_OBJECT_START,
    AGTYPE_TOKEN_OBJECT_END,
    AGTYPE_TOKEN_ARRAY_START,
    AGTYPE_TOKEN_ARRAY_END,
    AGTYPE_TOKEN_COMMA,
    AGTYPE_TOKEN_COLON,
    AGTYPE_TOKEN_ANNOTATION,
    AGTYPE_TOKEN_IDENTIFIER,
    AGTYPE_TOKEN_TRUE,
    AGTYPE_TOKEN_FALSE,
    AGTYPE_TOKEN_NULL,
    AGTYPE_TOKEN_END
} agtype_token_type;

/*
 * All the fields in this structure should be treated as read-only.
 *
 * If strval is not null, then it should contain the de-escaped value
 * of the lexeme if it's a string. Otherwise most of these field names
 * should be self-explanatory.
 *
 * line_number and line_start are principally for use by the parser's
 * error reporting routines.
 * token_terminator and prev_token_terminator point to the character
 * AFTER the end of the token, i.e. where there would be a nul byte
 * if we were using nul-terminated strings.
 */
typedef struct agtype_lex_context
{
    char *input;
    int input_length;
    char *token_start;
    char *token_terminator;
    char *prev_token_terminator;
    agtype_token_type token_type;
    int lex_level;
    int line_number;
    char *line_start;
    StringInfo strval;
} agtype_lex_context;

typedef void (*agtype_struct_action)(void *state);
typedef void (*agtype_ofield_action)(void *state, char *fname, bool isnull);
typedef void (*agtype_aelem_action)(void *state, bool isnull);
typedef void (*agtype_scalar_action)(void *state, char *token,
                                     agtype_token_type tokentype,
                                     char *annotation);
typedef void (*agtype_annotation_action)(void *state, char *annotation);

/*
 * Semantic Action structure for use in parsing agtype.
 * Any of these actions can be NULL, in which case nothing is done at that
 * point, Likewise, semstate can be NULL. Using an all-NULL structure amounts
 * to doing a pure parse with no side-effects, and is therefore exactly
 * what the agtype input routines do.
 *
 * The 'fname' and 'token' strings passed to these actions are palloc'd.
 * They are not free'd or used further by the parser, so the action function
 * is free to do what it wishes with them.
 */
typedef struct agtype_sem_action
{
    void *semstate;
    agtype_struct_action object_start;
    agtype_struct_action object_end;
    agtype_struct_action array_start;
    agtype_struct_action array_end;
    agtype_ofield_action object_field_start;
    agtype_ofield_action object_field_end;
    agtype_aelem_action array_element_start;
    agtype_aelem_action array_element_end;
    agtype_scalar_action scalar;
    /* annotations (typecast) */
    agtype_annotation_action agtype_annotation;
} agtype_sem_action;

/*
 * parse_agtype will parse the string in the lex calling the
 * action functions in sem at the appropriate points. It is
 * up to them to keep what state they need in semstate. If they
 * need access to the state of the lexer, then its pointer
 * should be passed to them as a member of whatever semstate
 * points to. If the action pointers are NULL the parser
 * does nothing and just continues.
 */
void parse_agtype(agtype_lex_context *lex, agtype_sem_action *sem);

/*
 * constructors for agtype_lex_context, with or without strval element.
 * If supplied, the strval element will contain a de-escaped version of
 * the lexeme. However, doing this imposes a performance penalty, so
 * it should be avoided if the de-escaped lexeme is not required.
 *
 * If you already have the agtype as a text* value, use the first of these
 * functions, otherwise use ag_make_agtype_lex_context_cstring_len().
 */
agtype_lex_context *make_agtype_lex_context(text *t, bool need_escapes);
agtype_lex_context *make_agtype_lex_context_cstring_len(char *str, int len,
                                                        bool need_escapes);

/*
 * Utility function to check if a string is a valid agtype number.
 *
 * str argument does not need to be null-terminated.
 */
extern bool is_valid_agtype_number(const char *str, int len);

extern char *agtype_encode_date_time(char *buf, Datum value, Oid typid);

#endif
