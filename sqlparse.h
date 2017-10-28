/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_ORAFCE_SQL_YY_SQLPARSE_H_INCLUDED
# define YY_ORAFCE_SQL_YY_SQLPARSE_H_INCLUDED
/* Debug traces.  */
#ifndef ORAFCE_SQL_YYDEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define ORAFCE_SQL_YYDEBUG 1
#  else
#   define ORAFCE_SQL_YYDEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define ORAFCE_SQL_YYDEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined ORAFCE_SQL_YYDEBUG */
#if ORAFCE_SQL_YYDEBUG
extern int orafce_sql_yydebug;
#endif

/* Token type.  */
#ifndef ORAFCE_SQL_YYTOKENTYPE
# define ORAFCE_SQL_YYTOKENTYPE
  enum orafce_sql_yytokentype
  {
    X_IDENT = 258,
    X_NCONST = 259,
    X_SCONST = 260,
    X_OP = 261,
    X_PARAM = 262,
    X_COMMENT = 263,
    X_WHITESPACE = 264,
    X_KEYWORD = 265,
    X_OTHERS = 266,
    X_TYPECAST = 267
  };
#endif

/* Value type.  */
#if ! defined ORAFCE_SQL_YYSTYPE && ! defined ORAFCE_SQL_YYSTYPE_IS_DECLARED

union ORAFCE_SQL_YYSTYPE
{
#line 60 "sqlparse.y" /* yacc.c:1909  */

	int 	ival;
	orafce_lexnode	*node;
	List		*list;
	struct
	{
		char 	*str;
		int		keycode;
		int		lloc;
		char	*sep;
		char *modificator;
	}				val;

#line 89 "sqlparse.h" /* yacc.c:1909  */
};

typedef union ORAFCE_SQL_YYSTYPE ORAFCE_SQL_YYSTYPE;
# define ORAFCE_SQL_YYSTYPE_IS_TRIVIAL 1
# define ORAFCE_SQL_YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined ORAFCE_SQL_YYLTYPE && ! defined ORAFCE_SQL_YYLTYPE_IS_DECLARED
typedef struct ORAFCE_SQL_YYLTYPE ORAFCE_SQL_YYLTYPE;
struct ORAFCE_SQL_YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define ORAFCE_SQL_YYLTYPE_IS_DECLARED 1
# define ORAFCE_SQL_YYLTYPE_IS_TRIVIAL 1
#endif


extern ORAFCE_SQL_YYSTYPE orafce_sql_yylval;
extern ORAFCE_SQL_YYLTYPE orafce_sql_yylloc;
int orafce_sql_yyparse (List **result);

#endif /* !YY_ORAFCE_SQL_YY_SQLPARSE_H_INCLUDED  */
