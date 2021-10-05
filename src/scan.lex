
%option noyywrap

%{
  #include <string.h>
  #include <stdlib.h>
  #include <stdio.h>
  #include "tree.h"
  #include "parsedef.h"
  #include "parse.h"
  #include "utils.h"
  #include "symtab.h"

#define YY_DECL int yylex (YYSTYPE *yylval, YYLTYPE *yylloc)

#define INC_COL(n) yylloc->last_column += (n)
#define INC_LINE ++yylloc->last_line; yylloc->last_column = 0;
#define NEW_TOKEN yylloc->first_line = yylloc->last_line; yylloc->first_column = yylloc->last_column;

%}

%x comment
%x string

FLOAT [0-9]+(\.[0-9]+(e[+-]?[0-9]+)?|(\.[0-9]+)?e[+-]?[0-9]+)
ID [a-zA-Z_]+[a-zA-Z_0-9]*

%%

%{
#define MAX_STR_LEN 2048
  char str_buf[MAX_STR_LEN];
  char *str_buf_ptr;
  char *str_buf_end;
%}
  

{FLOAT}       { NEW_TOKEN; INC_COL(yyleng); sscanf(yytext, "%lf", &yylval->dvalue); return DOUBLE; }
[0-9]+        { NEW_TOKEN; INC_COL(yyleng); sscanf(yytext, "%d", &yylval->ivalue); return INTEGER; }
"++"          { NEW_TOKEN; INC_COL(2); return PLUS_PLUS; }
"--"          { NEW_TOKEN; INC_COL(2); return MINUS_MINUS; }
"&&"          { NEW_TOKEN; INC_COL(2); return AND; }
"||"          { NEW_TOKEN; INC_COL(2); return OR; }
"=="          { NEW_TOKEN; INC_COL(2); return EQUAL; }
"!="          { NEW_TOKEN; INC_COL(2); return NOT_EQUAL; }
"<="          { NEW_TOKEN; INC_COL(2); return LESS_EQUAL; }
">="          { NEW_TOKEN; INC_COL(2); return GREATER_EQUAL; }
true          { NEW_TOKEN; INC_COL(4); yylval->bvalue = 1; return BOOLEAN; }
false         { NEW_TOKEN; INC_COL(5); yylval->bvalue = 0; return BOOLEAN; }
int           { NEW_TOKEN; INC_COL(3); yylval->type = TYPE_INT; return TYPE; }
double        { NEW_TOKEN; INC_COL(6); yylval->type = TYPE_DOUBLE; return TYPE; }
boolean       { NEW_TOKEN; INC_COL(7); yylval->type = TYPE_BOOLEAN; return TYPE; }
void          { NEW_TOKEN; INC_COL(4); yylval->type = TYPE_VOID; return TYPE; }
if            { NEW_TOKEN; INC_COL(2); return IF; }
else          { NEW_TOKEN; INC_COL(4); return ELSE; }
while         { NEW_TOKEN; INC_COL(5); return WHILE; }
for           { NEW_TOKEN; INC_COL(3); return FOR; }
return        { NEW_TOKEN; INC_COL(6); return RETURN; }
{ID}          { NEW_TOKEN; INC_COL(yyleng); yylval->symbol = add_sym(yytext); return ID; }

\"            { NEW_TOKEN; INC_COL(1); str_buf_ptr = str_buf; str_buf_end = str_buf + MAX_STR_LEN; BEGIN(string); }
<string>{
  \"          { INC_COL(1); yylval->str = xstrndup(str_buf, str_buf_ptr - str_buf); BEGIN(INITIAL); return STRING; }
  \\n         { INC_COL(2); if (str_buf_ptr == str_buf_end) xabort("string too long"); *str_buf_ptr++ = '\n'; }
  \\r         { INC_COL(2); if (str_buf_ptr == str_buf_end) xabort("string too long"); *str_buf_ptr++ = '\r'; }
  \\t         { INC_COL(2); if (str_buf_ptr == str_buf_end) xabort("string too long"); *str_buf_ptr++ = '\t'; }
  \\b         { INC_COL(2); if (str_buf_ptr == str_buf_end) xabort("string too long"); *str_buf_ptr++ = '\b'; }
  \\f         { INC_COL(2); if (str_buf_ptr == str_buf_end) xabort("string too long"); *str_buf_ptr++ = '\f'; }
  \\[0-7]{1,3} {
    int i;
    INC_COL(yyleng);
    if (str_buf_ptr == str_buf_end)
      {
	fatal(yylloc->first_line, yylloc->first_column, "string too long"); 
      }
    sscanf(yytext, "%o", &i);
    *str_buf_ptr++ = i;
  }
  \\[0-9]+    { error(yylloc->first_line, yylloc->first_column, "bad escape sequence"); INC_COL(yyleng); }
  \\.         { 
    INC_COL(2); 
    if (str_buf_ptr == str_buf_end) 
      {
	fatal(yylloc->first_line, yylloc->first_column, "string too long"); 
      }
    *str_buf_ptr++ = yytext[1]; 
  }
  \\\n        { 
    INC_LINE; 
    if (str_buf_ptr == str_buf_end)
      {
	fatal(yylloc->first_line, yylloc->first_column, "string too long");
      }
    *str_buf_ptr++ = yytext[1];
  }
  [^\\\n\"]+  { 
    int len = yyleng;
    INC_COL(len);
    if (len + str_buf_ptr > str_buf_end)
      {
	fatal(yylloc->first_line, yylloc->first_column, "string too long");
      }
    strncpy(str_buf_ptr, yytext, len);
    str_buf_ptr += len;
  }
  <<EOF>>     { error(yylloc->first_line, yylloc->first_column, "unterminated string"); yyterminate(); }
}

"/*"          { NEW_TOKEN; INC_COL(2); BEGIN(comment); }
<comment>{
  [^*\n]*     { INC_COL(yyleng); }
  \*+[^*/\n]* { INC_COL(yyleng); }
  \n          { INC_LINE; }
  \*+\/       { BEGIN(INITIAL); }
  <<EOF>>     { error(yylloc->first_line, yylloc->first_column, "unterminated comment"); yyterminate(); }
}

(\/\/|#)[^\n]* { INC_COL(yyleng); }
[ \t\r]+        { INC_COL(yyleng); }
\n            { INC_LINE; }
.             { NEW_TOKEN; INC_COL(1); return yytext[0]; }

%%

