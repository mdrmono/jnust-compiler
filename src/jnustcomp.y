%{
#include <iostream>
#include <ostream>
#include <string>
#include <cstdlib>
#include <list>
#include "jnust-defs.h"

int yylex(void);
int yyerror(char *);

// print AST?
bool printAST = false;

using namespace std;

// this global variable contains all the generated code
static llvm::Module *TheModule;

// this is the method used to construct the LLVM intermediate code (IR)
static llvm::LLVMContext TheContext;
static llvm::IRBuilder<> Builder(TheContext);
// the calls to TheContext in the init above and in the
// following code ensures that we are incrementally generating
// instructions in the right order

// dummy main function
// WARNING: this is not how you should implement code generation
// for the main function!
// You should write the codegen for the main method as
// part of the codegen for method declarations (MethodDecl)
static llvm::Function *TheFunction = 0;

// we have to create a main function
llvm::Function *gen_main_def() {
  // create the top-level definition for main
  llvm::FunctionType *FT = llvm::FunctionType::get(llvm::IntegerType::get(TheContext, 32), false);
  llvm::Function *TheFunction = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, "main", TheModule);
  if (TheFunction == 0) {
    throw runtime_error("empty function block");
  }
  // Create a new basic block which contains a sequence of LLVM instructions
  llvm::BasicBlock *BB = llvm::BasicBlock::Create(TheContext, "entry", TheFunction);
  // All subsequent calls to IRBuilder will place instructions in this location
  Builder.SetInsertPoint(BB);
  return TheFunction;
}

#include "jnustcomp.cc"

ProgramAST* root = NULL;

%}

%locations

%union{
    class jnustAST *ast;
    std::string *sval;
    char cval;
    int ival;
 }

%token <sval> T_ID
%token <sval> T_STRINGCONSTANT

%token <ival> T_INTCONSTANT

%token <cval> T_CHARCONSTANT

%token T_FUNC
%token T_PACKAGE
%token T_LCB
%token T_RCB
%token T_RPAREN
%token T_LPAREN
%token T_AND
%token T_INTTYPE
%token T_IF
%token T_RIGHTSHIFT
%token T_LEFTSHIFT
%token T_GT
%token T_LEQ
%token T_GEQ
%token T_ASSIGN
%token T_BOOLTYPE
%token T_BREAK
%token T_COMMA
%token T_COMMENT
%token T_CONTINUE
%token T_DIV
%token T_DOT
%token T_ELSE
%token T_EQ
%token T_LSB
%token T_LT
%token T_MINUS
%token T_EXTERN
%token T_FALSE
%token T_TRUE
%token T_FOR
%token T_MOD
%token T_MULT
%token T_NEQ
%token T_NOT
%token T_NULL
%token T_OR
%token T_PLUS
%token T_RETURN
%token T_RSB
%token T_SEMICOLON
%token T_STRINGTYPE
%token T_VAR
%token T_VOID
%token T_WHILE
%token T_WHITESPACE

%token UNARY_NOT
%token UNARY_MINUS

%left T_OR
%left T_AND
%left T_EQ T_NEQ T_LT T_LEQ T_GT T_GEQ
%left T_PLUS T_MINUS
%left T_MULT T_DIV T_MOD T_LEFTSHIFT T_RIGHTSHIFT
%right UNARY_NOT
%right UNARY_MINUS

%type <ast> extern_list jnustpackage externdefn
%type <ast> externtype_list externtype_list_no_param externtype methodtype type array_type non_array_type boolconstant constant
%type <ast> fielddecl_list fielddecl methoddecl_list methoddecl arguments
%type <ast> block methodblock vardecl_list vardecl statement_list statement
%type <ast> expression methodcall methodarg_list methodarg
%type <ast> assign_list assign identifier_list
%type <ast> binaryoperator arithmeticoperator booleanoperator

%%

start: program

program: extern_list jnustpackage
    {
      root = new ProgramAST((jnustStmtList *)$1, (PackageAST *)$2);
      if (printAST) {
        cout << getString(root) << endl;
      }
    }

extern_list: extern_list externdefn
    {
      jnustStmtList *slist = (jnustStmtList *)$1;
      slist->push_back($2);
      $$ = slist;
    }
    | externdefn
    {
      jnustStmtList *slist = new jnustStmtList();
      slist->push_back($1);
      $$ = slist;
    }
    |
    {
      jnustStmtList *slist = new jnustStmtList();
      $$ = slist;
    }
    ;

externdefn: T_EXTERN T_FUNC T_ID T_LPAREN externtype_list T_RPAREN methodtype T_SEMICOLON
    {
      $$ = new ExternFunction(*$3, (MethodType *)$7, (jnustStmtList *)$5);
      delete $3;
    }

externtype_list: externtype_list T_COMMA externtype
    {
      jnustStmtList *slist = (jnustStmtList *)$1;
      slist->push_back($3);
      $$ = slist;
    }
    | externtype
    {
      jnustStmtList *slist = new jnustStmtList();
      slist->push_back($1);
      $$ = slist;
    }
    |
    {
      $$ = new jnustStmtList();
    }
    ;

externtype: T_STRINGTYPE
    {
      $$ = new StringType();
    }
    | type
    {
      $$ = new ExternJnustType((JnustType *)$1);
    }
    ;

jnustpackage: T_PACKAGE T_ID T_LCB fielddecl_list methoddecl_list T_RCB
    {
      $$ = new PackageAST(*$2, (jnustStmtList *)$4, (jnustStmtList *)$5);
      delete $2;
    }
    ;

fielddecl_list: fielddecl fielddecl_list
    {
      jnustStmtList *slist = (jnustStmtList *)$2;
      for (auto *e : ((jnustStmtList *)$1)->get_statements()) {
        slist->push_back(e);
      }
      $$ = slist;
    }
    | fielddecl
    {
      jnustStmtList *slist = new jnustStmtList();
      for (auto *e : ((jnustStmtList *)$1)->get_statements()) {
        slist->push_back(e);
      }
      $$ = slist;
    }
    |
    {
      $$ = new jnustStmtList();
    }
    ;

fielddecl: T_VAR identifier_list type T_SEMICOLON
    {
      jnustStmtList *slist = new jnustStmtList();
      list<jnustAST *> list = ((jnustStmtList *)$2)->get_statements();
      for (std::list<jnustAST *>::reverse_iterator it=list.rbegin(); it!=list.rend(); ++it) {
        slist->push_back(new FieldDeclBasic((*it)->str(), (JnustType *)$3, new Scalar(), false));
      }
      $$ = slist;
    }
    | T_VAR T_ID type T_SEMICOLON
    {
      jnustStmtList *slist = new jnustStmtList;
      slist->push_back(new FieldDeclBasic(*$2, (JnustType *)$3, new Scalar(), false));
      $$ = slist;
      delete $2;
    }
    | T_VAR T_ID array_type T_SEMICOLON
    {
      jnustStmtList *slist = new jnustStmtList;
      ArrayJnustType* arr = (ArrayJnustType*)$3;
      slist->push_back(new FieldDeclBasic(*$2, arr, new ArraySize(arr->ArrSize()), true));
      $$ = slist;
      delete $2;
    }
    | T_VAR T_ID type T_ASSIGN constant T_SEMICOLON
    {
      jnustStmtList *slist = new jnustStmtList;
      slist->push_back(new FieldDeclBasic(*$2, (JnustType *)$3, new Scalar(), false, (Constant *)$5));
      $$ = slist;
      delete $2;
    }
    ;

methoddecl_list: methoddecl_list methoddecl
    {
      jnustStmtList *slist = (jnustStmtList *)$1;
      slist->push_back($2);
      $$ = slist;
    }
    | methoddecl
    {
      jnustStmtList *slist = new jnustStmtList();
      slist->push_back($1);
      $$ = slist;
    }
    |
    { $$ = new jnustStmtList(); }
    ;

methoddecl: T_FUNC T_ID T_LPAREN arguments T_RPAREN methodtype methodblock
    {
      $$ = new MethodDecl(*$2, (MethodType *)$6, (jnustStmtList *)$4, (MethodBlock *)$7);
      delete $2;
    }

arguments: arguments T_COMMA T_ID non_array_type
    {
      jnustStmtList *slist = (jnustStmtList *) $1;
      slist->push_back(new TypedSymbol(*$3, (JnustType *)$4));
      delete $3;
      $$ = slist;
    }
    | T_ID non_array_type
    {
      jnustStmtList *slist = new jnustStmtList();
      slist->push_back(new TypedSymbol(*$1, (JnustType *)$2));
      delete $1;
      $$ = slist;
    }
    |
    {
      $$ = new jnustStmtList();
    }
    ;

methodblock: T_LCB vardecl_list statement_list T_RCB
    {
      $$ = new MethodBlock((jnustStmtList *)$2, (jnustStmtList *)$3);
    }

block: T_LCB vardecl_list statement_list T_RCB
    {
      $$ = new Block((jnustStmtList *)$2, (jnustStmtList *)$3);
    }

vardecl_list: vardecl vardecl_list
    {
      jnustStmtList *slist = (jnustStmtList *)$2;
      for (auto *e : ((jnustStmtList *)$1)->get_statements()) {
        slist->push_back(e);
      }
      $$ = slist;
    }
    | vardecl
    {
      jnustStmtList *slist = new jnustStmtList();
      for (auto *e : ((jnustStmtList *)$1)->get_statements()) {
        slist->push_back(e);
      }
      $$ = slist;
    }
    |
    {
      $$ = new jnustStmtList();
    }
    ;

vardecl: T_VAR identifier_list non_array_type T_SEMICOLON
    {
      jnustStmtList *slist = new jnustStmtList();
      list<jnustAST *> list = ((jnustStmtList *)$2)->get_statements();
      for (std::list<jnustAST *>::reverse_iterator it=list.rbegin(); it!=list.rend(); ++it) {
        slist->push_back(new VarDecl((*it)->str(), (JnustType *)$3));
      }
      $$ = slist;
      delete $2;
    }

statement_list: statement_list statement
    {
      jnustStmtList *slist = (jnustStmtList *)$1;
      slist->push_back($2);
      $$ = slist;
    }
    | statement
    {
      jnustStmtList *slist = new jnustStmtList();
      slist->push_back($1);
      $$ = slist;
    }
    |
    {
      $$ = new jnustStmtList();
    }
    ;

statement: block
    { $$ = new BlockStatement((Block *)$1); }
    | assign T_SEMICOLON
    { $$ = new StatementAssign((Assign *)$1); }
    | methodcall T_SEMICOLON
    { $$ = new StatementMethodCall((MethodCall *)$1); }
    | T_IF T_LPAREN expression T_RPAREN block
    { $$ = new IfStatement((Expr *)$3, (Block *)$5); }
    | T_IF T_LPAREN expression T_RPAREN block T_ELSE block
    { $$ = new IfStatement((Expr *)$3, (Block *)$5, (Block *)$7); }
    | T_WHILE T_LPAREN expression T_RPAREN block
    { $$ = new WhileStatement((Expr *)$3, (Block *)$5); }
    | T_FOR T_LPAREN assign_list T_SEMICOLON expression T_SEMICOLON assign_list T_RPAREN block
    { $$ = new ForStatement((jnustStmtList *)$3, (Expr *)$5, (jnustStmtList *)$7, (Block *)$9); }
    | T_RETURN T_LPAREN expression T_RPAREN T_SEMICOLON
    { $$ = new ReturnStatement((Expr *)$3); }
    | T_RETURN T_LPAREN T_RPAREN T_SEMICOLON
    { $$ = new ReturnStatement(); }
    | T_RETURN T_SEMICOLON
    { $$ = new ReturnStatement(); }
    | T_BREAK T_SEMICOLON
    { $$ = new BreakStatement(); }
    | T_CONTINUE T_SEMICOLON
    { $$ = new ContinueStatement(); }
    ;

expression:
      T_ID
    {
      $$ = new ExprRvalue(new RvalueVariableExpr(*$1));
      delete $1;
    }
    | methodcall
    { $$ = new ExprMethodCall((MethodCall *)$1); }
    | constant
    { $$ = new ExprConstant((Constant *)$1); }
    | T_MINUS expression %prec UNARY_MINUS
    { $$ = new ExprUnary(new UnaryOperatorMinus() , (Expr *)$2); }
    | T_NOT expression %prec UNARY_NOT
    { $$ = new ExprUnary(new UnaryOperatorNot() , (Expr *)$2); }
    | T_LPAREN expression T_RPAREN
    { $$ = $2; }
    | T_ID T_LSB expression T_RSB
    {
      $$ = new ExprRvalue(new RvalueArrayLocExpr(*$1, (Expr *)$3));
      delete $1;
    }
    | expression T_MOD expression
    { $$ = new ExprBinary(new BinaryOperatorMod(), (Expr *)$1, (Expr *)$3); }
    | expression T_MULT expression
    { $$ = new ExprBinary(new BinaryOperatorMult(), (Expr *)$1, (Expr *)$3); }
    | expression T_PLUS expression
    { $$ = new ExprBinary(new BinaryOperatorPlus(), (Expr *)$1, (Expr *)$3); }
    | expression T_MINUS expression
    { $$ = new ExprBinary(new BinaryOperatorMinus(), (Expr *)$1, (Expr *)$3); }
    | expression T_DIV expression
    { $$ = new ExprBinary(new BinaryOperatorDiv(), (Expr *)$1, (Expr *)$3); }
    | expression T_GEQ expression
    { $$ = new ExprBinary(new BinaryOperatorGeq(), (Expr *)$1, (Expr *)$3); }
    | expression T_GT expression
    { $$ = new ExprBinary(new BinaryOperatorGt(), (Expr *)$1, (Expr *)$3); }
    | expression T_LT expression
    { $$ = new ExprBinary(new BinaryOperatorLt(), (Expr *)$1, (Expr *)$3); }
    | expression T_LEQ expression
    { $$ = new ExprBinary(new BinaryOperatorLeq(), (Expr *)$1, (Expr *)$3); }
    | expression T_NEQ expression
    { $$ = new ExprBinary(new BinaryOperatorNeq(), (Expr *)$1, (Expr *)$3); }
    | expression T_EQ expression
    { $$ = new ExprBinary(new BinaryOperatorEq(), (Expr *)$1, (Expr *)$3); }
    | expression T_OR expression
    { $$ = new ExprBinary(new BinaryOperatorOr(), (Expr *)$1, (Expr *)$3); }
    | expression T_AND expression
    { $$ = new ExprBinary(new BinaryOperatorAnd(), (Expr *)$1, (Expr *)$3); }
    | expression T_LEFTSHIFT expression
    { $$ = new ExprBinary(new BinaryOperatorLeftShift(), (Expr *)$1, (Expr *)$3); }
    | expression T_RIGHTSHIFT expression
    { $$ = new ExprBinary(new BinaryOperatorRightShift(), (Expr *)$1, (Expr *)$3); }
    ;


methodcall: T_ID T_LPAREN methodarg_list T_RPAREN
    {
      $$ = new MethodCall(*$1, (jnustStmtList *)$3);
      delete $1;
    }

methodarg_list: methodarg_list T_COMMA methodarg
    {
      jnustStmtList *slist = (jnustStmtList *)$1;
      slist->push_back($3);
      $$ = slist;
    }
    | methodarg
    {
      jnustStmtList *slist = new jnustStmtList();
      slist->push_back($1);
      $$ = slist;
    }
    |
    {
      jnustStmtList *slist = new jnustStmtList;
      $$ = slist;
    }
    ;

methodarg: expression
    { $$ = new MethodArgExpr((Expr *)$1); }
    | T_STRINGCONSTANT
    { $$ = new MethodArgString(*$1); }
    ;

assign_list: assign_list T_COMMA assign
    {
      jnustStmtList *slist = (jnustStmtList *)$1;
      slist->push_back($3);
      $$ = slist;
    }
    | assign
    {
      jnustStmtList *slist = new jnustStmtList();
      slist->push_back($1);
      $$ = slist;
    }
    ;

assign: T_ID T_ASSIGN expression
    {
      $$ = new AssignVar(*$1, (Expr *)$3);
      delete $1;
    }
    | T_ID T_LSB expression T_RSB T_ASSIGN expression
    {
      $$ = new AssignArrayLoc(*$1, (Expr *)$3, (Expr *)$6);
      delete $1;
    }

identifier_list: identifier_list T_COMMA T_ID
    {
      jnustStmtList *slist = (jnustStmtList *)$1;
      slist->push_back(new VarIdentifier(*$3));
      $$ = slist;
      delete $3;
    }
    | T_ID
    {
      jnustStmtList *slist = new jnustStmtList();
      slist->push_back(new VarIdentifier(*$1));
      $$ = slist;
      delete $1;
    }
    ;

binaryoperator: arithmeticoperator
    { $$ = $1; }
    | booleanoperator
    { $$ = $1; }
    ;

arithmeticoperator: T_PLUS
    { $$ = new BinaryOperatorPlus(); }
    | T_MINUS
    { $$ = new BinaryOperatorMinus(); }
    | T_MULT
    { $$ = new BinaryOperatorMult(); }
    | T_DIV
    { $$ = new BinaryOperatorDiv(); }
    | T_LEFTSHIFT
    { $$ = new BinaryOperatorLeftShift(); }
    | T_RIGHTSHIFT
    { $$ = new BinaryOperatorRightShift(); }
    | T_MOD
    { $$ = new BinaryOperatorMod(); }
    ;

booleanoperator: T_EQ
    { $$ = new BinaryOperatorEq(); }
    | T_NEQ
    { $$ = new BinaryOperatorNeq(); }
    | T_GT
    { $$ = new BinaryOperatorGt(); }
    | T_GEQ
    { $$ = new BinaryOperatorGeq(); }
    | T_LT
    { $$ = new BinaryOperatorLt(); }
    | T_LEQ
    { $$ = new BinaryOperatorLeq(); }
    | T_AND
    { $$ = new BinaryOperatorAnd(); }
    | T_OR
    { $$ = new BinaryOperatorOr(); }
    ;

non_array_type: T_INTTYPE
    { $$ = new IntType(); }
    | T_BOOLTYPE
    { $$ = new BoolType(); }
    ;

type: non_array_type
    | array_type
    ;

methodtype: T_VOID
    { $$ = new VoidType(); }
    | type
    { $$ = new MethodJnustType((JnustType *)$1); }
    ;

array_type: T_LSB T_INTCONSTANT T_RSB T_INTTYPE
    {
      $$ = new ArrayJnustType($2, "IntType");
    }
    | T_LSB T_INTCONSTANT T_RSB T_BOOLTYPE
    {
      $$ = new ArrayJnustType($2, "BoolType");
    }

boolconstant: T_TRUE
    { $$ = new ConstantBoolExpr(new BooleanValueTrue()); }
    | T_FALSE
    { $$ = new ConstantBoolExpr(new BooleanValueFalse()); }
    ;

constant: T_INTCONSTANT
    { $$ = new ConstantNumberExpr($1); }
    | T_CHARCONSTANT
    {
      $$ = new ConstantNumberExpr($1);
    }
    | boolconstant
    { $$ = $1; }
    ;

%%

int main() {
  // parse the input and create the abstract syntax tree
  TheModule = new llvm::Module("Jnust Module", TheContext);
  int retval = yyparse();
  if (retval >= 1) {
    return EXIT_FAILURE;
  }

  if (root == NULL) {
    cout << "empty root" << endl;
    return EXIT_SUCCESS;
  }

  packagestl->new_symtbl();
  jnustStmtList *packageMethods = root->getPackageAST()->getMethodList();
  for(auto* m : packageMethods->get_statements()){
    MethodDecl* method = static_cast<MethodDecl*>(m);
    method->decl();
  }

  root->Codegen();
  packagestl->pop_symtbl();
  TheModule->print(llvm::errs(), nullptr);

  delete root;
  delete TheModule;

  return EXIT_SUCCESS;
}
