%{
#include "jnust-defs.h"
#include "jnustcomp.tab.h"
#include <cstring>
#include <string>
#include <sstream>
#include <iostream>

using namespace std;

int lineno = 1;
int tokenpos = 1;

%}

%%
 /*
   Pattern definitions for all tokens
 */
func                             { return T_FUNC; }
package                          { return T_PACKAGE; }
\{                               { return T_LCB; }
\}                               { return T_RCB; }
\(                               { return T_LPAREN; }
\)                               { return T_RPAREN; }
\&\&                             { return T_AND; }
int                              { return T_INTTYPE; }
\<\<                             { return T_LEFTSHIFT; }
if                               { return T_IF; }
\>\>                             { return T_RIGHTSHIFT; }
\>                               { return T_GT; }
\<\=                             { return T_LEQ; }
\>\=                             { return T_GEQ; }
\=                               { return T_ASSIGN; }
bool                             { return T_BOOLTYPE; }
break                            { return T_BREAK; }
\,                               { return T_COMMA; }
\/\/.*\n                         { }
continue                         { return T_CONTINUE; }
\/                               { return T_DIV; }
\.                               { return T_DOT; }
else                             { return T_ELSE; }
\=\=                             { return T_EQ; }
\[                               { return T_LSB; }
\<                               { return T_LT; }
\-                               { return T_MINUS; }
extern                           { return T_EXTERN; }
false                            { return T_FALSE; }
true                             { return T_TRUE; }
for                              { return T_FOR; }
\%                               { return T_MOD; }
\*                               { return T_MULT; }
\!                               { return T_NOT; }
\!\=                             { return T_NEQ; }
null                             { return T_NULL; }
\|\|                             { return T_OR; }
\+                               { return T_PLUS; }
return                           { return T_RETURN; }
\]                               { return T_RSB; }
\;                               { return T_SEMICOLON; }
string                           { return T_STRINGTYPE; }
var                              { return T_VAR; }
void                             { return T_VOID; }
while                            { return T_WHILE; }
\'\\[nrtvfab\\\'\"]\'            {
  char esc = yytext[2];
  switch (esc) {
    case 'n': yylval.cval = '\n'; break;
    case 'r': yylval.cval = '\r'; break;
    case 't': yylval.cval = '\t'; break;
    case 'v': yylval.cval = '\v'; break;
    case 'f': yylval.cval = '\f'; break;
    case 'a': yylval.cval = '\a'; break;
    case 'b': yylval.cval = '\b'; break;
    case '\\': yylval.cval = '\\'; break;
    case '\'': yylval.cval = '\''; break;
    case '\"': yylval.cval = '\"'; break;
  }
  return T_CHARCONSTANT;
}
\'[^\']\'                       {
  yylval.cval = yytext[1];
  return T_CHARCONSTANT;
}
\"(\\[nrtvfab\\\'\"]|[^\\\"\n])*\" {
  yylval.sval = new string(yytext);
  return T_STRINGCONSTANT;
}
0[xX][0-9a-fA-F]+ {
  int result = 0;
  int len = strlen(yytext);

  for (int i = 0; i < len; ++i) {
    char c = yytext[i];
    int digit = 0;

    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = 10 + (c - 'a');
    } else if (c >= 'A' && c <= 'F') {
      digit = 10 + (c - 'A');
    }

    result = result * 16 + digit;
  }

  yylval.ival = result;
  return T_INTCONSTANT;
}
[0-9]+ {
  int result = 0;
  int len = strlen(yytext);

  for (int i = 0; i < len; ++i) {
    char c = yytext[i];
    int digit = c - '0';
    result = result * 10 + digit;
  }

  yylval.ival = result;
  return T_INTCONSTANT;
}
[\r\t\v\f ]+                     { }
[\r\t\v\f \n]+                   {
  for(int i = 0; i < yyleng; i++){
    if(yytext[i]=='\n'){
      lineno++;
      tokenpos = 1;
    }
    else {
      tokenpos++;
    }
  }
  yylloc.first_line = lineno;
  yylloc.first_column = tokenpos;
  yylloc.last_line = lineno;
  yylloc.last_column = tokenpos;
}
[a-zA-Z\_][a-zA-Z\_0-9]* {
  yylval.sval = new string(yytext);
  return T_ID;
}

\"(\\[^nrtvfab\\\'\"]|[^\\\"\n])*\"           {
    cerr << "Error: unknown escape sequence in string constant\n";
    cerr << "Lexical error: line " << lineno << ", position " << tokenpos << endl;
    exit(EXIT_FAILURE);
} /*unknown escape error*/
\"(\\.|[^\"\n\\])*\n        {
    cerr << "Error: newline in string constant\n";
    cerr << "Lexical error: line " << lineno << ", position " << tokenpos << endl;
    exit(EXIT_FAILURE);
} /*newline in string*/
\"(\\.|[^\"\n\\])*([^"]|$)  {
    cerr << "Error: string constant is missing closing delimiter\n";
    cerr << "Lexical error: line " << lineno << ", position " << tokenpos << endl;
    exit(EXIT_FAILURE);
} /*no missing end quote in string*/
\'(\\[nrtvfab\\\'\"]|[^\'\n\\]){2,}\' {
    cerr << "Error: char constant length is greater than one\n";
    cerr << "Lexical error: line " << lineno << ", position " << tokenpos << endl;
    exit(EXIT_FAILURE);
} /*char greater than 2 in length*/
\'(\\[nrtvfab\\\'\"]|[^\'\n\\])*([^']|$) {
    cerr << "Error: unterminated char constant\n";
    cerr << "Lexical error: line " << lineno << ", position " << tokenpos << endl;
    exit(EXIT_FAILURE);
} /*char missing end quote*/
\'\' {
    cerr << "Error: char constant has zero width\n";
    cerr << "Lexical error: line " << lineno << ", position " << tokenpos << endl;
    exit(EXIT_FAILURE);
} /*char has width of zero*/
. {
    cerr << "Error: unexpected character in input\n";
    cerr << "Lexical error: line " << lineno << ", position " << tokenpos << endl;
    exit(EXIT_FAILURE);
} /*unexpected character in input*/

%%

int yyerror(const char *s) {
  cerr << lineno << ": " << s << " at char " << tokenpos << endl;
  return 1;
}
