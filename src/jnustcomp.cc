#include "jnust-defs.h"
#include <list>
#include <ostream>
#include <iostream>
#include <sstream>

#include <utility>      // std::pair, std::make_pair
#include <string>       // std::string
#include <map>          // std::map
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <iterator>
#include <algorithm>
#include <stack>

#ifndef YYTOKENTYPE
#include "jnustcomp.tab.h"
#endif

std::stack<llvm::BasicBlock*> breakbb;
std::stack<llvm::BasicBlock*> continuebb;
extern llvm::IRBuilder<> Builder;
extern llvm::LLVMContext TheContext;
extern llvm::Module *TheModule;
extern llvm::Function *TheFunction;

using namespace std;

struct Symbol {
  llvm::Value* value;
  int lineno;
  int tokenpos;
  Symbol(llvm::Value* value, int lineno, int tokenpos)
  :
  value(value),
  lineno(lineno),
  tokenpos(tokenpos)
  {};
};

class symboltable { // got from https://github.com/anoopsarkar/compilers-class-hw/blob/master/yacc-practice/symboltable.cc

public:

  symboltable() {
  }

  void new_symtbl() {
    symbol_table *new_symtbl = new symbol_table();
    symtbl.push_front(new_symtbl);
  }

  void pop_symtbl() {
    if (symtbl.empty())
      throw runtime_error("no table");
    symtbl.pop_front();
  }


  void remove_symtbl() {
    symbol_table *tabl;
    if (symtbl.empty())
      throw runtime_error("no table");
    else
      tabl = symtbl.front();
      tabl->clear();
      delete(tabl);
    symtbl.pop_front();
  }

  void enter_symtbl(string ident, Symbol *val) {
    symbol_table *tabl;

    if (symtbl.empty())
      throw runtime_error("no table");

    tabl = symtbl.front();
    auto find_ident = tabl->find(ident);
    if (find_ident != tabl->end()) {
      tabl->erase(ident);
    }
    (*tabl)[ident] = val;
  }

  Symbol* access_symtbl(string ident) {
    for (symbol_table_list::iterator i = symtbl.begin(); i != symtbl.end(); ++i) {
      symbol_table::iterator find_ident;
      if ((find_ident = (*i)->find(ident)) != (*i)->end()) return find_ident->second;
    }
    return NULL;
  }

private:
  typedef map<string, Symbol*> symbol_table;
  typedef list<symbol_table*> symbol_table_list;
  symbol_table_list symtbl;
};

symboltable *stl = new symboltable();
symboltable *externstl = new symboltable();
symboltable *packagestl = new symboltable();

//based on https://medium.com/@sohail_saifi/building-a-custom-compiler-with-llvm-an-advanced-guide-for-language-design-996fb9722751
llvm::AllocaInst *defineVariable(llvm::Function *func, llvm::Type *llvmTy, string ident) {
    llvm::IRBuilder<> done(&func->getEntryBlock(), func->getEntryBlock().begin());
    llvm::AllocaInst* Alloca = done.CreateAlloca(llvmTy, 0, ident.c_str());
    return Alloca;
}

// lexer return string is not suitable
string modifyStringConstant(const string& str) {
  string result;

  for (size_t i = 1; i < str.size() - 1; i++) {
    if (str[i] == '\\') {
      i++;
      switch (str[i]) {
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 'v': result += '\v'; break;
        case 'f': result += '\f'; break;
        case 'a': result += '\a'; break;
        case 'b': result += '\b'; break;
        case '\\': result += '\\'; break;
        case '\'': result += '\''; break;
        case '\"': result += '\"'; break;
      }
    } else {
      result += str[i];
    }
  }
  return result;
}

/// jnustAST - Base class for all abstract syntax tree nodes.
class jnustAST {
public:
  virtual ~jnustAST() {}
  virtual string str() { return string(""); }
  virtual llvm::Value *Codegen() = 0;
};

string getString(jnustAST *d) {
  if (d != NULL) {
    return d->str();
  } else {
    return string("None");
  }
}

template <class T>
string commaList(list<T> vec) {
    string s("");
    for (typename list<T>::iterator i = vec.begin(); i != vec.end(); i++) {
        s = s + (s.empty() ? string("") : string(",")) + (*i)->str();
    }
    if (s.empty()) {
        s = string("None");
    }
    return s;
}

template <class T>
llvm::Value *listCodegen(list<T> vec) {
  llvm::Value *val = NULL;
  for (typename list<T>::iterator i = vec.begin(); i != vec.end(); i++) {
    llvm::Value *j = (*i)->Codegen();
    if (j != NULL) { val = j; }
  }
  return val;
}

/// jnustStmtList - List of Jnust statements
class jnustStmtList : public jnustAST {
  list<jnustAST *> stmts;
public:
  jnustStmtList() {}
  ~jnustStmtList() {
    for (list<jnustAST *>::iterator i = stmts.begin(); i != stmts.end(); i++) {
      delete *i;
    }
  }
  int size() { return stmts.size(); }
  void push_front(jnustAST *e) { stmts.push_front(e); }
  void push_back(jnustAST *e) { stmts.push_back(e); }
  list<jnustAST *> get_statements() { return stmts; }
  string str() { return commaList<class jnustAST *>(stmts); }
  llvm::Value *Codegen() {
    return listCodegen<jnustAST *>(stmts);
  }
};

class PackageAST : public jnustAST {
  string Name;
  jnustStmtList *FieldDeclList;
  jnustStmtList *MethodDeclList;
public:
  PackageAST(string name, jnustStmtList *fieldlist, jnustStmtList *methodlist)
    : Name(name), FieldDeclList(fieldlist), MethodDeclList(methodlist) {}
  ~PackageAST() {
    if (FieldDeclList != NULL) { delete FieldDeclList; }
    if (MethodDeclList != NULL) { delete MethodDeclList; }
  }
  string str() {
    return string("Package") + "(" + Name + "," + getString(FieldDeclList) + "," + getString(MethodDeclList) + ")";
  }

  jnustStmtList *getMethodList() { return MethodDeclList; }

  llvm::Value *Codegen() {
    stl->new_symtbl();
    llvm::Value *val = NULL;
    TheModule->setModuleIdentifier(llvm::StringRef(Name));

    if (NULL != FieldDeclList) {
      val = FieldDeclList->Codegen();
    }
    if (NULL != MethodDeclList) {
      val = MethodDeclList->Codegen();
    }
    // Q: should we enter the class name into the symbol table?
    stl->pop_symtbl();
    return val;
  }
  string get_name() const { return Name; }
};

/// ProgramAST - the jnust program
class ProgramAST : public jnustAST {
  jnustStmtList *ExternList;
  PackageAST *PackageDef;
public:
  ProgramAST(jnustStmtList *externs, PackageAST *c) : ExternList(externs), PackageDef(c) {}
  ~ProgramAST() {
    if (ExternList != NULL) { delete ExternList; }
    if (PackageDef != NULL) { delete PackageDef; }
  }
  string str() { return string("Program") + "(" + getString(ExternList) + "," + getString(PackageDef) + ")"; }

  PackageAST *getPackageAST() { return PackageDef; }

  llvm::Value *Codegen() {
    externstl->new_symtbl();
    llvm::Value *val = NULL;
    if (NULL != ExternList) {
      val = ExternList->Codegen();
    }
    if (NULL != PackageDef) {
      val = PackageDef->Codegen();
    } else {
      throw runtime_error("no package definition in jnust program");
    }
    externstl->pop_symtbl();
    return val;
  }
};

class JnustType : public jnustAST {
public:
  virtual ~JnustType() {}
  virtual string str() override { return string(""); }
  virtual string strSem() { return string(""); }
  virtual llvm::Type* get_type() = 0;
  llvm::Value* Codegen() override {
    return nullptr; // Temporary placeholder
}
};

class IntType : public JnustType {
public:
  string str() override { return string("IntType"); }
  string strSem() override { return string("int"); }
  llvm::Type* get_type() override {
    return llvm::Type::getInt32Ty(TheContext);
  }
};

class BoolType : public JnustType {
public:
  string str() override { return string("BoolType"); }
  string strSem() override { return string("bool"); }
  llvm::Type* get_type() override {
    return llvm::Type::getInt1Ty(TheContext);
  }
};

class MethodType : public jnustAST {
public:
  virtual ~MethodType() {}
  virtual string str() { return string(""); }
  virtual string strSem() { return string(""); }
  virtual llvm::Type* get_type() = 0;
};

class VoidType : public MethodType {
public:
  string str() override { return string("VoidType"); }
  string strSem() override { return string("void"); }
  llvm::Type* get_type() override {
    return llvm::Type::getVoidTy(TheContext);
  }
  llvm::Value* Codegen() override;
};
llvm::Value* VoidType::Codegen() {
  return nullptr;
}

class MethodJnustType : public MethodType {
  JnustType *type;
public:
  MethodJnustType(JnustType *type) : type(type) {}
  string str() override { return getString(type); }
  llvm::Type* get_type(){
    return type->get_type();
  }
  llvm::Value* Codegen() override;
};
llvm::Value* MethodJnustType::Codegen() {
  return nullptr;
}

class TypedSymbol : public jnustAST {
  string name;
  JnustType *type;
public:
  TypedSymbol(string name, JnustType *type) : name(name), type(type) {}

  ~TypedSymbol() {
    if (type != NULL) { delete type; }
  }

  string str() override { return string("VarDef") + "(" + name + "," + getString(type) + ")"; }
  llvm::Type* get_type(){
    return type->get_type();
  }
  string get_name() const { return name; }
  llvm::Value *Codegen() override;
};
llvm::Value *TypedSymbol::Codegen(){
  llvm::AllocaInst *tmp = defineVariable(TheFunction, type->get_type(), name);
  stl->enter_symtbl(name, new Symbol(tmp, lineno, tokenpos));
  return tmp;
}

class ExternType : public jnustAST {
public:
  virtual ~ExternType() {}
  virtual string str() { return string(""); }
  virtual llvm::Type* get_type() = 0;
  virtual llvm::Value* Codegen() {
    return nullptr;
  }
};

class ExternFunction : public jnustAST {
  string name;
  MethodType *return_type;
  jnustStmtList *type_list;
public:
  ExternFunction(string name, MethodType *return_type, jnustStmtList *type_list)
  :
  name(name),
  return_type(return_type),
  type_list(type_list)
  {}

  ~ExternFunction() {
    if (type_list != NULL) { delete type_list; }
  }

  string str() { return string("ExternFunction") + "(" + name + "," + getString((jnustAST *)return_type) + "," + getString(type_list) + ")"; }

  string get_name() const { return name; }
  llvm::Value *Codegen() override;
};
llvm::Value *ExternFunction::Codegen() {
    llvm::Type *ret = return_type->get_type();
    vector<llvm::Type *> args;

    if (type_list) {
        for (auto i : type_list->get_statements()) {
          ExternType *temp = static_cast<ExternType*>(i);
          llvm::Type *temp_type = temp->get_type();
          args.push_back(temp_type);
        }
    }
    llvm::FunctionType *final_code = llvm::FunctionType::get(ret, args, false);
    llvm::Function *final2 = llvm::Function::Create(final_code, llvm::Function::ExternalLinkage, name, TheModule);
    externstl->enter_symtbl(name, new Symbol(final2, lineno, tokenpos));

    return final2;
}

class ExternJnustType : public ExternType {
  JnustType *type;
public:
  ExternJnustType(JnustType *type) : type(type) {}
  string str() override { return string("VarDef") + "(" + getString(type) + ")"; }
  llvm::Type* get_type() override {
    return type->get_type();
  }
};

class StringType : public ExternType {
public:
  string str() override { return string("VarDef") + "(" + string("StringType") + ")"; }
  llvm::Type* get_type() override {
    return llvm::PointerType::get(llvm::Type::getInt8Ty(TheContext), 0);
  }
};

class FieldSize : public jnustAST {
public:
    virtual ~FieldSize() {}
    virtual string str() const = 0;
    virtual llvm::Value* Codegen() override = 0;
    virtual int get_size() const = 0;
};

class Scalar : public FieldSize {
public:
    string str() const override { return "Scalar"; }
    llvm::Value* Codegen() override {
        return llvm::ConstantInt::get(Builder.getInt32Ty(), 1);
    }
    int get_size() const override { return 1; }
};

class ArraySize : public FieldSize {
    int size;
public:
    ArraySize(int size) : size(size) {}
    string str() const override { return "Array(" + to_string(size) + ")"; }
    llvm::Value* Codegen() override {
        return llvm::ConstantInt::get(Builder.getInt32Ty(), size);
    }
    int get_size() const override { return size; }
};

class Constant : public jnustAST {
public:
  virtual ~Constant() {}
  virtual string str() override { return string(""); }
  llvm::Value *Codegen() override {
    return nullptr;
  }
};

class ConstantNumberExpr : public Constant {
  int value;
public:
  ConstantNumberExpr(int value) : value(value) {}

  string str() override { return string("NumberExpr") + "(" + to_string(value) + ")"; }
  llvm::Value *Codegen() override;
};
llvm::Value *ConstantNumberExpr::Codegen() {
  return llvm::ConstantInt::get(Builder.getInt32Ty(), value, true);
}



class BooleanValue : public jnustAST {
public:
  virtual ~BooleanValue() {}
  virtual string str() override { return string(""); }
  virtual bool get_bool() const = 0;

};

class BooleanValueTrue : public BooleanValue {
public:
  string str() override { return string("True"); }
  bool get_bool() const override { return true; }
  llvm::Value* Codegen() override;
};
llvm::Value *BooleanValueTrue::Codegen(){
  return llvm::ConstantInt::get(Builder.getInt1Ty(), true);
}

class BooleanValueFalse : public BooleanValue {
public:
  virtual string str() override { return string("False"); }
  bool get_bool() const override { return false; }
  llvm::Value* Codegen() override;
};
llvm::Value *BooleanValueFalse::Codegen(){
  return llvm::ConstantInt::get(Builder.getInt1Ty(), false);
}

class ConstantBoolExpr : public Constant {
  BooleanValue *value;
public:
  ConstantBoolExpr(BooleanValue *value) : value(value) {}
  llvm::Value *Codegen() override;
  string str() override { return string("BoolExpr") + "(" + getString(value) + ")"; }
};
llvm::Value *ConstantBoolExpr::Codegen() {
  return llvm::ConstantInt::get(Builder.getInt1Ty(), value->get_bool());
}


class FieldDecl : public jnustAST {
public:
  virtual ~FieldDecl() {}
  virtual string str() { return string(""); }
};


class ArrayJnustType : public JnustType {
  int arrsize;
  string type;
public:
  ArrayJnustType(int arrsize, string type) : arrsize(arrsize), type(type) {}
  int ArrSize() const { return arrsize; }
  string str() override { return string("ArrayType") + "(" + to_string(arrsize) + "," + type + ")";  }
  string getString() const { return type; }
  llvm::ArrayType* get_type() override {
    if (type == "BoolType") {
      return llvm::ArrayType::get(Builder.getInt1Ty(), arrsize);
    } else if (type == "IntType") {
      return llvm::ArrayType::get(Builder.getInt32Ty(), arrsize);
    } else {
      throw std::runtime_error("Unknown element type in ArrayJnustType: " + type);
    }
  }
};

class FieldDeclBasic : public FieldDecl {
  string name;
  JnustType *type;
  FieldSize *size;
  bool array;
  Constant *value;
public:
  FieldDeclBasic(string name, JnustType *type, FieldSize *size, bool array, Constant *value = nullptr)
  :
  name(name),
  type(type),
  size(size),
  array(array),
  value(value)
  {}

  string str() override { return string("FieldDecl") + "(" + name + "," + getString(type) + "," + getString((FieldSize *)size) + ")"; }
  string get_name() const { return name; }
  llvm::Value *Codegen() override;
};
llvm::Value *FieldDeclBasic::Codegen(){
  //check if we are working with array or var
  if(array) {
    ArraySize* check = static_cast<ArraySize*>(size);
    ArrayJnustType* arrayCheck = static_cast<ArrayJnustType*>(type);
    llvm::ArrayType* atype = arrayCheck->get_type();

    llvm::Constant* array = llvm::Constant::getNullValue(atype);

    llvm::GlobalVariable* var = new llvm::GlobalVariable(*TheModule, atype, false, llvm::GlobalValue::ExternalLinkage, array, name);
    stl->enter_symtbl(name, new Symbol(var, lineno, tokenpos));

    return var;
  }
  //other only 1 var
  llvm::Type* vartype = type->get_type();
  llvm::Constant* rhs = nullptr;

  if (value != nullptr) {
    llvm::Value* val = value->Codegen();
    if (llvm::isa<llvm::Constant>(val)) {
      rhs = static_cast<llvm::Constant*>(val);
    } else {
      throw runtime_error("Global variable initializer must be a constant");
    }

    if (rhs->getType() != vartype) {
      if (vartype->isIntegerTy(32) && rhs->getType()->isIntegerTy(1)) {
        rhs = llvm::ConstantExpr::getCast(llvm::Instruction::ZExt, rhs, vartype);
      } else if (vartype->isIntegerTy(1) && rhs->getType()->isIntegerTy(32)) {
        rhs = llvm::ConstantExpr::getCast(llvm::Instruction::Trunc, rhs, vartype);
      }
    }
  } else {
    rhs = llvm::Constant::getNullValue(vartype);
  }

  llvm::GlobalVariable* var = new llvm::GlobalVariable(*TheModule, vartype, false, llvm::GlobalValue::ExternalLinkage, rhs, name);
  stl->enter_symtbl(name, new Symbol(var, lineno, tokenpos));
  return var;
}


class VarDecl : public jnustAST {
  string name;
  JnustType *type;
public:
  VarDecl(string name, JnustType *type)
  :
  name(name),
  type(type)
  {}

  string str() override { return string("VarDecl") + "(" + name + "," + getString(type) + ")"; }
  string get_name() const { return name; }
  llvm::Value *Codegen() override;
};
llvm::Value *VarDecl::Codegen(){
  llvm::AllocaInst* Alloca = defineVariable(TheFunction, type->get_type(), name);
  Builder.CreateStore(llvm::Constant::getNullValue(type->get_type()), Alloca);
  Symbol* symb = new Symbol(Alloca, lineno, tokenpos);
  stl->enter_symtbl(name, symb);
  return Alloca;
}

class MethodBlock : public jnustAST {
  jnustStmtList *var_decl_list;
  jnustStmtList *statement_list;
public:
  MethodBlock(jnustStmtList *var_decl_list, jnustStmtList *statement_list)
  :
  var_decl_list(var_decl_list),
  statement_list(statement_list)
  {}

  ~MethodBlock() {
    if (var_decl_list != NULL) { delete var_decl_list; }
    if (statement_list != NULL) { delete statement_list; }
  }

  string str() override { return string("MethodBlock") + "(" + getString(var_decl_list) + "," + getString(statement_list) + ")"; }
  llvm::Value* Codegen() override;
};
llvm::Value *MethodBlock::Codegen(){
  if(var_decl_list != nullptr){
    for(auto i : var_decl_list->get_statements()){
      i->Codegen();
      if (Builder.GetInsertBlock()->getTerminator()) {
        break;
      }
    }
  }
  if(statement_list != nullptr){
    for(auto x : statement_list->get_statements()){
      x->Codegen();
      if (Builder.GetInsertBlock()->getTerminator()) {
        break;
      }
    }
  }

  return Builder.GetInsertBlock();
}

class Block : public jnustAST {
  jnustStmtList *var_decl_list;
  jnustStmtList *statement_list;
public:
  Block(jnustStmtList *var_decl_list, jnustStmtList *statement_list)
  :
  var_decl_list(var_decl_list),
  statement_list(statement_list)
  {}

  ~Block() {
    if (var_decl_list != NULL) { delete var_decl_list; }
    if (statement_list != NULL) { delete statement_list; }
  }

  string str() override { return string("Block") + "(" + getString(var_decl_list) + "," + getString(statement_list) + ")"; }
  llvm::Value *Codegen() override;
};

llvm::Value *Block::Codegen(){
  stl->new_symtbl();
  if(var_decl_list != nullptr){
    for(auto i : var_decl_list->get_statements()){
      i->Codegen();
      if (Builder.GetInsertBlock()->getTerminator()) {
        break;
      }
    }
  }
  if(statement_list != nullptr){
    for(auto x : statement_list->get_statements()){
      x->Codegen();
      if (Builder.GetInsertBlock()->getTerminator()) {
        break;
      }
    }
  }
  llvm::BasicBlock* bb = Builder.GetInsertBlock();
  stl->pop_symtbl();
  return bb;
}

class MethodDecl : public jnustAST {
  string name;
  MethodType *method_type;
  jnustStmtList *param_list;
  MethodBlock* block;
public:
  MethodDecl(string name, MethodType *method_type, jnustStmtList *param_list, MethodBlock* block)
  :
  name(name),
  method_type(method_type),
  param_list(param_list),
  block(block)
  {}

  ~MethodDecl() {
    if (param_list != NULL) { delete param_list; }
  }
  string get_name() const { return name; }

  string str() override { return string("Method") + "(" + name + "," + getString(method_type) + "," + getString(param_list) + "," + getString(block) + ")"; }

  llvm::Value *Codegen() override;

  void decl() {
    vector<llvm::Type *> args;
    for(auto* i : param_list->get_statements()){
      TypedSymbol* arg = static_cast<TypedSymbol*>(i);
      args.push_back(arg->get_type());
    }

    llvm::Function *func = llvm::Function::Create(
      llvm::FunctionType::get(method_type->get_type(), args, false), llvm::Function::ExternalLinkage, name, TheModule);
    //put on table
    packagestl->enter_symtbl(name, new Symbol(func, lineno, tokenpos));
  }
};

llvm::Value* MethodDecl::Codegen()  {
  Symbol* sym = packagestl->access_symtbl(name);
  llvm::Function *func = static_cast<llvm::Function*>(sym->value);

  int x = 0;

  TheFunction = func;

  llvm::BasicBlock *ebb = llvm::BasicBlock::Create(TheContext, "entry", TheFunction);
  Builder.SetInsertPoint(ebb);

  stl->new_symtbl();
  for (auto i : param_list->get_statements()) {
    TypedSymbol* param = static_cast<TypedSymbol *>(i);
    llvm::AllocaInst *Alloca = static_cast<llvm::AllocaInst *>(param->Codegen());
    Builder.CreateStore(func->getArg(x), Alloca);
    x++;
  }

  llvm::Value* b = block->Codegen();

  if (!Builder.GetInsertBlock()->getTerminator()) {
    if (method_type->get_type()->isVoidTy()) {
      Builder.CreateRetVoid();
    } else if (method_type->get_type()->isIntegerTy(1)) {
      Builder.CreateRet(llvm::ConstantInt::get(method_type->get_type(), 1));
    } else {
      Builder.CreateRet(llvm::ConstantInt::get(method_type->get_type(), 0));
    }
  }

  stl->pop_symtbl();
  return TheFunction;
}

class Expr : public jnustAST {
public:
  virtual ~Expr() {}
  virtual string str() override { return string(""); }
  llvm::Value *Codegen() override {
    return nullptr;
  }
};

class Rvalue : public jnustAST {
public:
  virtual ~Rvalue() {}
  virtual string str() override { return string(""); }
  llvm::Value *Codegen() override {
    return nullptr;
  }
};

class RvalueVariableExpr : public Rvalue {
  string name;
public:
  RvalueVariableExpr(string name) : name(name) {}

  string str() override { return string("VariableExpr") + "(" + name + ")"; }
  string get_name() const { return name; }
  llvm::Value *Codegen() override;

};
llvm::Value *RvalueVariableExpr::Codegen() {
  Symbol *sym = stl->access_symtbl(name);
  if (!sym) {
    throw runtime_error("Use of undeclared variable '" + name + "' at line " + to_string(lineno));
  }
  llvm::Value *val = sym->value;
  llvm::Type* type;
  if (llvm::isa<llvm::AllocaInst>(val)) {
    type = static_cast<llvm::AllocaInst*>(val)->getAllocatedType();
  } else if (llvm::isa<llvm::GlobalVariable>(val)) {
    type = static_cast<llvm::GlobalVariable*>(val)->getValueType();
  } else {
    type = val->getType();
  }
  return Builder.CreateLoad(type, val, name.c_str());
}

class RvalueArrayLocExpr : public Rvalue {
  string name;
  Expr *index;
public:
  RvalueArrayLocExpr(string name, Expr *index) : name(name), index(index) {}

  string str() override { return string("ArrayLocExpr") + "(" + name + "," + getString(index) + ")"; }
  string get_name() const { return name; }
  llvm::Value *Codegen() override;
};
llvm::Value *RvalueArrayLocExpr::Codegen() {
    Symbol *sym = stl->access_symtbl(name);
    if (!sym) {
    throw runtime_error("Use of undeclared variable '" + name + "' at line " + to_string(lineno));
    }
    //get the array, and the part in brackets (ival), make array global since all arrays in jnust are global scope
    llvm::Value *array = sym->value;
    llvm::Value *ival = index->Codegen();
    auto *a = static_cast<llvm::GlobalVariable*>(array);
    if(a->getValueType()->isArrayTy() != true){
      throw runtime_error("Invalid array type");
    }

    if (ival->getType()->isIntegerTy(1)) {
      throw runtime_error("Array index cannot be a boolean. Line: " + to_string(lineno));
    }

    //get array type, type of values in array
    //also make sure element is int or bool as thats only valid types in jnust
    llvm::ArrayType *atype = static_cast<llvm::ArrayType*>(a->getValueType());
    llvm::Type *etype = atype->getElementType();
    if(etype->isIntegerTy(32) != true && etype->isIntegerTy(1) != true){
      throw runtime_error("invalid elem type");
    }


    //used for calculating addresses
    vector<llvm::Value*> args;
    args.push_back(Builder.getInt32(0));
    args.push_back(ival);

    //create array pointer, return value at that location
    llvm::Value *array_loc = Builder.CreateInBoundsGEP(atype, array, args, "arrayloc");
    return Builder.CreateLoad(etype, array_loc, name + "_value");
}

class ExprRvalue : public Expr {
  Rvalue *rvalue;
public:
  ExprRvalue(Rvalue *rvalue) : rvalue(rvalue) {}

  string str() override { return getString(rvalue); }
  llvm::Value* Codegen() override;
};

llvm::Value* ExprRvalue::Codegen()  {
  return rvalue->Codegen();
}

class MethodCall : public jnustAST {
  string name;
  jnustStmtList *method_arg_list;
public:
  MethodCall(string name, jnustStmtList *method_arg_list) : name(name), method_arg_list(method_arg_list) {}

  ~MethodCall() {
    if (method_arg_list != NULL) { delete method_arg_list; }
  }

  string str() override { return string("MethodCall") + "(" + name + "," + getString(method_arg_list) + ")"; }
  string get_name() const { return name; }
  llvm::Value *Codegen() override;
};
llvm::Value *MethodCall::Codegen(){
  Symbol *sym = stl->access_symtbl(name);
  if (sym == NULL) {
    sym = packagestl->access_symtbl(name);
    if (sym == NULL) {
      sym = externstl->access_symtbl(name);
      if (sym == NULL) {
        throw runtime_error("no method " + name);
      }
    }
  }

  llvm::Function *func = (llvm::Function *)sym->value;
  vector<llvm::Value *> args;
  for(auto i : method_arg_list->get_statements()){
    llvm::Value* val = i->Codegen();
    args.push_back(val);
  }


  if (func->getReturnType()->isVoidTy()) {
    return Builder.CreateCall(func, args);
  } else {
    return Builder.CreateCall(func, args, "calltmp");
  }
}

class MethodArg : public jnustAST {
public:
  virtual ~MethodArg() {}
  virtual string str() override { return string(""); }
  llvm::Value *Codegen() override {
    return nullptr;
  }
};

class MethodArgString : public MethodArg {
  string value;
public:
  MethodArgString(string value) : value(value) {}

  string str() override { return string("StringConstant") + "(" + value + ")"; }
  llvm::Value *Codegen() override;
};
llvm::Value *MethodArgString::Codegen() {
  return Builder.CreateGlobalString(modifyStringConstant(value), "tmp");
}


class MethodArgExpr : public MethodArg {
  Expr *expr;
public:
  MethodArgExpr(Expr *expr) : expr(expr) {}

  string str() override { return getString(expr); }
  llvm::Value *Codegen() override;
};
llvm::Value *MethodArgExpr::Codegen() {
  return expr->Codegen();
}

class ExprMethodCall : public Expr {
  MethodCall *method_call;
public:
  ExprMethodCall(MethodCall *method_call) : method_call(method_call) {}

  string str() override { return getString(method_call); }
  llvm::Value* Codegen() override;
};
llvm::Value* ExprMethodCall::Codegen(){
  return method_call->Codegen();
}

class ExprConstant : public Expr {
  Constant *constant;
public:
  ExprConstant(Constant *constant) : constant(constant) {}

  string str() override { return getString(constant); }
  llvm::Value* Codegen() override;
};
llvm::Value* ExprConstant::Codegen(){
  return constant->Codegen();
}

class BinaryOperator : public jnustAST {
public:
  virtual ~BinaryOperator() {}
  virtual string str() override { return string(""); }
  llvm::Value *Codegen() override {
    return nullptr;
  }
};

class BinaryOperatorPlus : public BinaryOperator {
public:
  virtual string str() override { return string("Plus"); }
};
class BinaryOperatorMinus : public BinaryOperator {
public:
  virtual string str() override { return string("Minus"); }
};
class BinaryOperatorMult : public BinaryOperator {
public:
  virtual string str() override { return string("Mult"); }
};
class BinaryOperatorDiv : public BinaryOperator {
public:
  virtual string str() override { return string("Div"); }
};
class BinaryOperatorLeftShift : public BinaryOperator {
public:
  virtual string str() override { return string("Leftshift"); }
};
class BinaryOperatorRightShift : public BinaryOperator {
public:
  virtual string str() override { return string("Rightshift"); }
};
class BinaryOperatorMod : public BinaryOperator {
public:
  virtual string str() override { return string("Mod"); }
};
class BinaryOperatorLt : public BinaryOperator {
public:
  virtual string str() override { return string("Lt"); }
};
class BinaryOperatorGt : public BinaryOperator {
public:
  virtual string str() override { return string("Gt"); }
};
class BinaryOperatorLeq : public BinaryOperator {
public:
  virtual string str() override { return string("Leq"); }
};
class BinaryOperatorGeq : public BinaryOperator {
public:
  virtual string str() override { return string("Geq"); }
};
class BinaryOperatorEq : public BinaryOperator {
public:
  virtual string str() override { return string("Eq"); }
};
class BinaryOperatorNeq : public BinaryOperator {
public:
  virtual string str() override { return string("Neq"); }
};
class BinaryOperatorAnd : public BinaryOperator {
public:
  virtual string str() override { return string("And"); }
};
class BinaryOperatorOr : public BinaryOperator {
public:
  virtual string str() override { return string("Or"); }
};

class ExprBinary : public Expr {
  BinaryOperator *op;
  Expr *left_value;
  Expr *right_value;
public:
  ExprBinary(BinaryOperator *op, Expr *left_value, Expr *right_value)
  :
  op(op),
  left_value(left_value),
  right_value(right_value)
  {}

  string str() override { return string("BinaryExpr") + "(" + getString(op) + "," + getString(left_value) + "," + getString(right_value) + ")"; }
  llvm::Value *Codegen() override;
};
llvm::Value *ExprBinary::Codegen(){
  TheFunction = Builder.GetInsertBlock()->getParent();
  string sym = op->str();
  llvm::Value *L = nullptr;
  llvm::Value *R = nullptr;

  // Handle short-circuit operators first
  if (sym == "And") {
    L = left_value->Codegen();

    if (Builder.GetInsertBlock()->getTerminator()) {
      return L;
    }

    // Convert to i1 if needed
    if (L->getType()->isIntegerTy(32)) {
      L = Builder.CreateICmpNE(L, llvm::ConstantInt::get(L->getType(), 0), "toBoolLeft");
    }

    llvm::BasicBlock *ebb = Builder.GetInsertBlock();
    llvm::BasicBlock *normalbb = llvm::BasicBlock::Create(TheContext, "and_normal");
    llvm::BasicBlock *mbb = llvm::BasicBlock::Create(TheContext, "and_merge");

    Builder.CreateCondBr(L, normalbb, mbb);
    TheFunction->insert(TheFunction->end(), normalbb);
    Builder.SetInsertPoint(normalbb);

    R = right_value->Codegen();

    if (Builder.GetInsertBlock()->getTerminator()) {
      TheFunction->insert(TheFunction->end(), mbb);
      Builder.SetInsertPoint(mbb);

      llvm::PHINode *val = Builder.CreatePHI(llvm::Type::getInt1Ty(TheContext), 1, "and_phival");
      val->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(TheContext), 0), ebb);

      return val;
    }

    // Convert to i1 if needed
    if (R->getType()->isIntegerTy(32)) {
      R = Builder.CreateICmpNE(R, llvm::ConstantInt::get(R->getType(), 0), "toBoolRight");
    }

    llvm::BasicBlock *validbb = Builder.GetInsertBlock();
    Builder.CreateBr(mbb);
    TheFunction->insert(TheFunction->end(), mbb);
    Builder.SetInsertPoint(mbb);

    llvm::PHINode *val = Builder.CreatePHI(llvm::Type::getInt1Ty(TheContext), 2, "and_phival");
    val->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(TheContext), 0), ebb);
    val->addIncoming(R, validbb);

    return val;
  }

  if(sym == "Or") {
    L = left_value->Codegen();

    if (Builder.GetInsertBlock()->getTerminator()) {
      return L;
    }

    if (L->getType()->isIntegerTy(32)) {
      L = Builder.CreateICmpNE(L, llvm::ConstantInt::get(L->getType(), 0), "toBoolLeft");
    }

    llvm::BasicBlock *ebb = Builder.GetInsertBlock();
    llvm::BasicBlock *normalbb = llvm::BasicBlock::Create(TheContext, "or_normal");
    llvm::BasicBlock *mbb = llvm::BasicBlock::Create(TheContext, "or_merge");

    Builder.CreateCondBr(L, mbb, normalbb);
    TheFunction->insert(TheFunction->end(), normalbb);
    Builder.SetInsertPoint(normalbb);

    R = right_value->Codegen();

    if (Builder.GetInsertBlock()->getTerminator()) {
      TheFunction->insert(TheFunction->end(), mbb);
      Builder.SetInsertPoint(mbb);

      llvm::PHINode *val = Builder.CreatePHI(llvm::Type::getInt1Ty(TheContext), 1, "or_phival");
      val->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(TheContext), 1), ebb);

      return val;
    }

    if (R->getType()->isIntegerTy(32)) {
      R = Builder.CreateICmpNE(R, llvm::ConstantInt::get(R->getType(), 0), "toBoolRight");
    }

    llvm::BasicBlock *validbb = Builder.GetInsertBlock();
    Builder.CreateBr(mbb);
    TheFunction->insert(TheFunction->end(), mbb);
    Builder.SetInsertPoint(mbb);

    llvm::PHINode *val = Builder.CreatePHI(llvm::Type::getInt1Ty(TheContext), 2, "or_phival");
    val->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(TheContext), 1), ebb);
    val->addIncoming(R, validbb);

    return val;
  }

  L = left_value->Codegen();

  if (Builder.GetInsertBlock()->getTerminator()) {
    return L;
  }

  R = right_value->Codegen();

  if (Builder.GetInsertBlock()->getTerminator()) {
    return R;
  }

  bool needsPromotion = (sym == "Plus" || sym == "Minus" || sym == "Mult" || sym == "Div" || sym == "Leftshift" || sym == "Rightshift" || sym == "Mod" || sym == "Lt" || sym == "Gt" || sym == "Leq" || sym == "Geq");

  if (needsPromotion && L->getType() != R->getType()) {
    if(L->getType()->isIntegerTy(1)){
      L = Builder.CreateZExt(L, llvm::Type::getInt32Ty(TheContext));
    }
    if (R->getType()->isIntegerTy(1)) {
      R = Builder.CreateZExt(R, llvm::Type::getInt32Ty(TheContext));
    }
  }

  if(sym == "Plus") return Builder.CreateAdd(L, R, "addtmp");
  if(sym == "Minus") return Builder.CreateSub(L, R, "subtmp");
  if(sym == "Mult") return Builder.CreateMul(L, R, "multmp");
  if(sym == "Div") return Builder.CreateSDiv(L, R, "divtmp");
  if(sym == "Leftshift") return Builder.CreateShl(L, R, "shltmp");
  if(sym == "Rightshift") return Builder.CreateLShr(L, R, "lshrtmp");
  if(sym == "Mod") return Builder.CreateSRem(L, R, "modtmp");
  if(sym == "Lt") return Builder.CreateICmpSLT(L, R, "lttmp");
  if(sym == "Gt") return Builder.CreateICmpSGT(L, R, "gttmp");
  if(sym == "Leq") return Builder.CreateICmpSLE(L, R, "ltetmp");
  if(sym == "Geq") return  Builder.CreateICmpSGE(L, R, "gtetmp");
  if(sym == "Eq") return Builder.CreateICmpEQ(L, R, "eqtmp");
  if(sym == "Neq") return Builder.CreateICmpNE(L, R, "neqtmp");

  throw runtime_error("invalid symbol");
}

class UnaryOperator : public jnustAST {
public:
  virtual ~UnaryOperator() {}
  virtual string str() override { return string(""); }
  llvm::Value *Codegen() override {
    return nullptr;
  }
};

class UnaryOperatorMinus : public UnaryOperator {
public:
  virtual string str() override { return string("UnaryMinus"); }
};

class UnaryOperatorNot : public UnaryOperator {
public:
  virtual string str() override { return string("Not"); }
};

class ExprUnary : public Expr {
  UnaryOperator *op;
  Expr *value;
public:
  ExprUnary(UnaryOperator *op, Expr *value)
  :
  op(op),
  value(value)
  {}

  string str() override { return string("UnaryExpr") + "(" + getString(op) + "," + getString(value) + ")"; }
  llvm::Value *Codegen() override;
};
llvm::Value *ExprUnary::Codegen(){
  string sym = op->str();
  llvm::Value *val = value->Codegen();
  if(sym == "Not") return Builder.CreateNot(val, "nottmp");
  if(sym == "UnaryMinus") return Builder.CreateNeg(val, "negtmp");
  throw runtime_error("invalid symbol");
}





class Assign : public jnustAST {
public:
  virtual ~Assign() {}
  virtual string str() override { return string(""); }
  llvm::Value *Codegen() override {
    return nullptr;
  }
};

class AssignVar : public Assign {
  string name;
  Expr *value;
public:
  AssignVar(string name, Expr *value) : name(name), value(value) {}

  string str() override { return string("AssignVar") + "(" + name + "," + getString(value) + ")"; }
  string get_name() const { return name; }
  llvm::Value *Codegen() override;
};

llvm::Value *AssignVar::Codegen(){
  Symbol *sym = stl->access_symtbl(name);
  if (!sym) {
    throw runtime_error("Use of undeclared variable '" + name + "' at line " + to_string(lineno));
  }

  llvm::Value *value_res = value->Codegen();
  if (!value_res) {
    throw runtime_error("No value resulting from expression.");
  }

  llvm::Type* targetType;
  if (llvm::isa<llvm::AllocaInst>(sym->value)) {
    targetType = static_cast<llvm::AllocaInst*>(sym->value)->getAllocatedType();
  } else if (llvm::isa<llvm::GlobalVariable>(sym->value)) {
    targetType = static_cast<llvm::GlobalVariable*>(sym->value)->getValueType();
  } else {
    throw runtime_error("Invalid target for assignment: " + name);
  }

  if (value_res->getType() != targetType) {
    if (targetType->isIntegerTy(32) && value_res->getType()->isIntegerTy(1)) {
      value_res = Builder.CreateZExt(value_res, targetType, "boolToInt");
    } else if (targetType->isIntegerTy(1) && value_res->getType()->isIntegerTy(32)) {
      value_res = Builder.CreateTrunc(value_res, targetType, "intToBool");
    } else {
      throw runtime_error("Type mismatch in assignment to " + name + ". Expected: " + std::to_string(targetType->getTypeID()) + ", Got: " + std::to_string(value_res->getType()->getTypeID()));
    }
  }

  return Builder.CreateStore(value_res, sym->value);
}

class AssignArrayLoc : public Assign {
  string name;
  Expr *index;
  Expr *value;
public:
  AssignArrayLoc(string name, Expr *index, Expr *value) : name(name), index(index), value(value) {}

  string str() override { return string("AssignArrayLoc") + "(" + name + "," + getString(index) + "," + getString(value) + ")"; }
  string get_name() const { return name; }
  llvm::Value *Codegen() override;
};
llvm::Value *AssignArrayLoc::Codegen() {
  Symbol *sym = stl->access_symtbl(name);
  if (!sym) {
  throw runtime_error("Use of undeclared variable '" + name + "' at line " + to_string(lineno));
  }

  llvm::Value *array = sym->value;
  llvm::Value *ival = index->Codegen();
  auto *a = static_cast<llvm::GlobalVariable*>(array);
  if(a->getValueType()->isArrayTy() != true){
    throw runtime_error("invalid array type");
  }

  llvm::ArrayType *atype = static_cast<llvm::ArrayType*>(a->getValueType());
  llvm::Type *etype = atype->getElementType();
  if(etype->isIntegerTy(32) != true && etype->isIntegerTy(1) != true){
    throw runtime_error("invalid elem type");
  }

  //used for calculating addresses
  vector<llvm::Value*> args;
  args.push_back(Builder.getInt32(0));
  args.push_back(ival);

  //create array pointer, return value at that location
  llvm::Value *array_loc = Builder.CreateInBoundsGEP(atype, array, args, "arrayloc");

  llvm::Value *val = value->Codegen();
  if (val->getType() != etype) {
    if (etype->isIntegerTy(32) && val->getType()->isIntegerTy(1)) {
        val = Builder.CreateZExt(val, etype, "bool_to_int");
    } else if (etype->isIntegerTy(1) && val->getType()->isIntegerTy(32)) {
        val = Builder.CreateTrunc(val, etype, "int_to_bool");
    }
  }

  return Builder.CreateStore(val, array_loc);
}

class Statement : public jnustAST {
public:
  virtual ~Statement() {}
  virtual string str() override { return string(""); }
  llvm::Value *Codegen() override {
    return nullptr;
  }
};

class StatementAssign : public Statement {
  Assign *assign;
public:
  StatementAssign(Assign *assign) : assign(assign) {}

  string str() override { return getString(assign); }
  llvm::Value* Codegen() override;
};
llvm::Value *StatementAssign::Codegen(){
  return assign->Codegen();
}

class StatementMethodCall : public Statement {
  MethodCall *method_call;
public:
  StatementMethodCall(MethodCall *method_call) : method_call(method_call) {}

  string str() override { return getString(method_call); }
  llvm::Value* Codegen() override;

};
llvm::Value* StatementMethodCall::Codegen(){
  if(!method_call){
    throw runtime_error("no method");
  }
  return method_call->Codegen();
}

class IfStatement : public Statement {
  Expr *condition;
  Block *if_block;
  Block *else_block;
public:
  IfStatement(Expr *condition, Block *if_block, Block *else_block = nullptr)
  :
  condition(condition),
  if_block(if_block),
  else_block(else_block)
  {}

  string str() override { return string("IfStmt") + "(" + getString(condition) + "," + getString(if_block) + "," + getString(else_block) + ")"; }
  llvm::Value* Codegen() override;

};
llvm::Value* IfStatement::Codegen() {
  if (Builder.GetInsertBlock()->getTerminator()) {
    return nullptr;
  }

  llvm::Value *cond = condition->Codegen();

  if (Builder.GetInsertBlock()->getTerminator()) {
    return nullptr;
  }

  llvm::BasicBlock *ibb = llvm::BasicBlock::Create(TheContext, "if", TheFunction);
  llvm::BasicBlock *ebb = llvm::BasicBlock::Create(TheContext, "else");
  llvm::BasicBlock *mbb = llvm::BasicBlock::Create(TheContext, "merge");

  Builder.CreateCondBr(cond, ibb, ebb);

  Builder.SetInsertPoint(ibb);
  if_block->Codegen();
  if (!Builder.GetInsertBlock()->getTerminator()) {
    Builder.CreateBr(mbb);
  }

  TheFunction->insert(TheFunction->end(), ebb);
  Builder.SetInsertPoint(ebb);
  if (else_block) {
    else_block->Codegen();
  }

  if (!Builder.GetInsertBlock()->getTerminator()) {
    Builder.CreateBr(mbb);
  }

  TheFunction->insert(TheFunction->end(), mbb);
  Builder.SetInsertPoint(mbb);
  return ibb;
}

class WhileStatement : public Statement {
  Expr *condition;
  Block *while_block;
public:
  WhileStatement(Expr *condition, Block *while_block)
  :
  condition(condition),
  while_block(while_block)
  {}

  string str() override { return string("WhileStmt") + "(" + getString(condition) + "," + getString(while_block) + ")";}
  llvm::Value* Codegen() override;
};
llvm::Value* WhileStatement::Codegen() {
  if (Builder.GetInsertBlock()->getTerminator()) {
    return nullptr;
  }

  //loop blocks
  llvm::BasicBlock *compare = llvm::BasicBlock::Create(TheContext, "condition", TheFunction);
  llvm::BasicBlock *loop = llvm::BasicBlock::Create(TheContext, "loop");
  llvm::BasicBlock *end = llvm::BasicBlock::Create(TheContext, "end");

  //check condition
  Builder.CreateBr(compare);
  Builder.SetInsertPoint(compare);
  llvm::Value *cond = condition->Codegen();
  if (!cond->getType()->isIntegerTy(1)) {
    cond = Builder.CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
  }

  //jump to loop if true, else jump to end
  Builder.CreateCondBr(cond, loop, end);
  TheFunction->insert(TheFunction->end(), loop);
  Builder.SetInsertPoint(loop);

  //use stacks to manage control flow, then create loop body
  breakbb.push(end);
  continuebb.push(compare);

  while_block->Codegen();

  breakbb.pop();
  continuebb.pop();
  //re compare condition
  Builder.CreateBr(compare);

  //put end
  TheFunction->insert(TheFunction->end(), end);
  Builder.SetInsertPoint(end);
  return end;
}

class ForStatement : public Statement {
  jnustStmtList *pre_assign_list;
  Expr *condition;
  jnustStmtList *loop_assign_list;
  Block *for_block;
public:
  ForStatement(jnustStmtList *pre_assign_list, Expr *condition, jnustStmtList *loop_assign_list, Block *for_block)
  :
  pre_assign_list(pre_assign_list),
  condition(condition),
  loop_assign_list(loop_assign_list),
  for_block(for_block)
  {}

  ~ForStatement() {
    if (pre_assign_list != NULL) { delete pre_assign_list; }
    if (loop_assign_list != NULL) { delete loop_assign_list; }
  }

  llvm::Value *Codegen() override;
  string str() override { return string("ForStmt") + "(" + getString(pre_assign_list) + "," + getString(condition) + "," + getString(loop_assign_list) + "," + getString(for_block) + ")";}
};
llvm::Value* ForStatement::Codegen() {
  if (Builder.GetInsertBlock()->getTerminator()) {
    return nullptr;
  }

  //loop blocks
  llvm::BasicBlock *compare = llvm::BasicBlock::Create(TheContext, "condition", TheFunction);
  llvm::BasicBlock *loop = llvm::BasicBlock::Create(TheContext, "loop");
  llvm::BasicBlock *end = llvm::BasicBlock::Create(TheContext, "end");
  llvm::BasicBlock *plusplus = llvm::BasicBlock::Create(TheContext, "plus");

  //execute code for initial variables
  if(pre_assign_list != NULL){
    for(auto i : pre_assign_list->get_statements()){
      i->Codegen();
    }
  }

  //check condition
  Builder.CreateBr(compare);
  Builder.SetInsertPoint(compare);
  llvm::Value *cond = condition->Codegen();
  if (!cond->getType()->isIntegerTy(1)) {
    cond = Builder.CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
  }


  //jump to loop if true, else jump to end
  Builder.CreateCondBr(cond, loop, end);
  TheFunction->insert(TheFunction->end(), loop);
  Builder.SetInsertPoint(loop);

  //use stacks to manage control flow, then create loop body
  breakbb.push(end);
  continuebb.push(plusplus);
  for_block->Codegen();
  breakbb.pop();
  continuebb.pop();

  //update i variable
  Builder.CreateBr(plusplus);
  TheFunction->insert(TheFunction->end(), plusplus);
  Builder.SetInsertPoint(plusplus);

  //execute code after loop is done like increment i
  if(loop_assign_list != NULL){
    for(auto i : loop_assign_list->get_statements()){
      i->Codegen();
    }
  }
  //re compare
  Builder.CreateBr(compare);

  //end block
  TheFunction->insert(TheFunction->end(), end);
  Builder.SetInsertPoint(end);
  return end;
}


class ReturnStatement : public Statement {
  Expr *return_value;
public:
  ReturnStatement(Expr *return_value = nullptr)
  :
  return_value(return_value)
  {}

  string str() override { return string("ReturnStmt") + "(" + getString(return_value) + ")";}
  llvm::Value *Codegen() override;
};
llvm::Value *ReturnStatement::Codegen(){
  llvm::Function* func = Builder.GetInsertBlock()->getParent();
  llvm::Type* retType = func->getReturnType();

  if(return_value){
    return Builder.CreateRet(return_value->Codegen());
  } else {
    if (retType->isVoidTy()) {
      return Builder.CreateRetVoid();
    } else if (retType->isIntegerTy(1)) {
      return Builder.CreateRet(llvm::ConstantInt::get(retType, 1));
    } else {
      return Builder.CreateRet(llvm::ConstantInt::get(retType, 0));
    }
  }
}

class BreakStatement : public Statement {
public:
  string str() override { return string("BreakStmt"); }
  llvm::Value *Codegen() override;
};
llvm::Value *BreakStatement::Codegen(){
  return Builder.CreateBr(breakbb.top());
}

class ContinueStatement : public Statement {
public:
  string str() override { return string("ContinueStmt"); }
  llvm::Value *Codegen() override;
};
llvm::Value *ContinueStatement::Codegen(){
  return Builder.CreateBr(continuebb.top());
}

class BlockStatement : public Statement {
  Block *block;
public:
  BlockStatement(Block *block) : block(block) {}
  string str() override { return getString(block); }
  llvm::Value *Codegen() override;
};
llvm::Value* BlockStatement::Codegen(){
  if(!block){
    return nullptr;
  }
  return block->Codegen();
}

class VarIdentifier : public jnustAST {
  string name;
public:
  VarIdentifier(string name) : name(name) {}
  string str() override { return name; }
  string get_name() const { return name; }
  llvm::Value* Codegen() override;

};
llvm::Value *VarIdentifier::Codegen(){
  return nullptr;
}
