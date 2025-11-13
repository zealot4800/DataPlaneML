/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_YACC_HPP_INCLUDED
# define YY_YY_YACC_HPP_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    BIT = 258,                     /* BIT  */
    STRUCT = 259,                  /* STRUCT  */
    TRUE = 260,                    /* TRUE  */
    FALSE = 261,                   /* FALSE  */
    HEADER = 262,                  /* HEADER  */
    HEADERS = 263,                 /* HEADERS  */
    PARSER = 264,                  /* PARSER  */
    STATE = 265,                   /* STATE  */
    TRANSITION = 266,              /* TRANSITION  */
    PACKET = 267,                  /* PACKET  */
    PACKET_IN = 268,               /* PACKET_IN  */
    DEFAULT = 269,                 /* DEFAULT  */
    ACCEPT = 270,                  /* ACCEPT  */
    EXTRACT = 271,                 /* EXTRACT  */
    SELECT = 272,                  /* SELECT  */
    ACTIONS = 273,                 /* ACTIONS  */
    ACTION = 274,                  /* ACTION  */
    NOACTION = 275,                /* NOACTION  */
    TABLES = 276,                  /* TABLES  */
    TABLE = 277,                   /* TABLE  */
    KEY = 278,                     /* KEY  */
    SIZE = 279,                    /* SIZE  */
    DEFAULT_ACTION = 280,          /* DEFAULT_ACTION  */
    ENTRIES = 281,                 /* ENTRIES  */
    EXACT = 282,                   /* EXACT  */
    TERNARY = 283,                 /* TERNARY  */
    LPM = 284,                     /* LPM  */
    STAGE = 285,                   /* STAGE  */
    CONTROL = 286,                 /* CONTROL  */
    MATCHER = 287,                 /* MATCHER  */
    EXECUTOR = 288,                /* EXECUTOR  */
    SWITCH = 289,                  /* SWITCH  */
    HIT = 290,                     /* HIT  */
    MISS = 291,                    /* MISS  */
    NONE = 292,                    /* NONE  */
    IS_VALID = 293,                /* IS_VALID  */
    VALUE_WIDTH = 294,             /* VALUE_WIDTH  */
    EQU = 295,                     /* EQU  */
    NEQ = 296,                     /* NEQ  */
    LEQ = 297,                     /* LEQ  */
    GEQ = 298,                     /* GEQ  */
    T_EOF = 299,                   /* T_EOF  */
    ANDANDAND = 300,               /* ANDANDAND  */
    DIVDIVDIV = 301,               /* DIVDIVDIV  */
    LSHIFT = 302,                  /* LSHIFT  */
    RSHIFT = 303,                  /* RSHIFT  */
    IDENTIFIER = 304,              /* IDENTIFIER  */
    VALUE_STRING = 305,            /* VALUE_STRING  */
    VALUE_INT = 306                /* VALUE_INT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif




int yyparse (void);


#endif /* !YY_YY_YACC_HPP_INCLUDED  */
