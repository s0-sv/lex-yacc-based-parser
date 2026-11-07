#pragma once
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

enum class TK { Void, Bool, Char, Short, Int, Long, Float, Double, Ptr, Array, Struct, Func };

struct StructInfo;

struct Type {
    TK k = TK::Int;
    bool uns = false;
    Type* base = nullptr;
    long len = -1;
    StructInfo* st = nullptr;
    std::vector<Type*> params;
    bool variadic = false;
    bool noproto = false;
};

struct Field {
    std::string name;
    Type* type;
    long offset;
};

struct StructInfo {
    std::string tag;
    bool isUnion = false;
    bool complete = false;
    std::vector<Field> fields;
    long size = 0;
    long align = 1;
};

extern Type* tyVoid;
extern Type* tyBool;
extern Type* tyChar;
extern Type* tyUChar;
extern Type* tyShort;
extern Type* tyUShort;
extern Type* tyInt;
extern Type* tyUInt;
extern Type* tyLong;
extern Type* tyULong;
extern Type* tyFloat;
extern Type* tyDouble;

Type* mkPtr(Type* base);
Type* mkArray(Type* base, long len);
long sizeOf(Type* t);
long alignOf(Type* t);
bool isInt(Type* t);
bool isFp(Type* t);
bool isArith(Type* t);
bool isPtr(Type* t);
bool isScalar(Type* t);
bool isAgg(Type* t);
bool sameType(Type* a, Type* b);
std::string typeStr(Type* t, const std::string& inner = "");

enum class Op {
    None, Add, Sub, Mul, Div, Mod, Shl, Shr, BitAnd, BitOr, BitXor,
    Lt, Le, Gt, Ge, Eq, Ne, LAnd, LOr, Neg, Pos, Not, BitNot
};

bool isCompare(Op op);
long normInt(Type* t, long v);
long intBinary(Op op, Type* t, long a, long b, bool* divZero);
double fpBinary(Op op, bool single, double a, double b);
long fpCompare(Op op, double a, double b);
long intCompare(Op op, bool uns, long a, long b);
double castToFp(Type* from, Type* to, long i, double d);
long castToInt(Type* from, Type* to, long i, double d);

enum class NK {
    TranslationUnit, FunctionDef, Declaration, VarDecl, ParamDecl, Specifiers, Spec, TypedefRef,
    StructSpec, EnumSpec, Enumerator, FieldDecl, FieldDeclarator, DeclName, PointerDecl, ArrayDecl,
    FuncDecl, TypeName, InitList, Designated, DesigField, DesigIndex,
    Block, ExprStmt, If, While, DoWhile, For, Switch, Case, Default, Break, Continue, Return, Goto,
    Label, Empty,
    Ident, IntLit, FloatLit, CharLit, StrLit, Binary, Assign, CompoundAssign, Unary, AddrOf, Deref,
    PreInc, PreDec, PostInc, PostDec, Cast, ImplicitCast, SizeofExpr, SizeofType, Call, Index,
    Member, Ternary, Comma
};

struct Node;
struct FuncInfo;

enum class SymKind { Var, Func, Typedef, EnumConst };

struct Symbol {
    std::string name;
    std::string label;
    SymKind kind = SymKind::Var;
    Type* type = nullptr;
    bool global = false;
    bool hasStorage = false;
    bool internal = false;
    bool builtin = false;
    bool used = false;
    long offset = 0;
    long value = 0;
    long addr = 0;
    int id = 0;
    FuncInfo* fn = nullptr;
    int line = 0;
};

struct InitOp {
    long offset;
    Type* type;
    Node* expr;
    int byte;
};

struct FuncInfo {
    Symbol* sym = nullptr;
    Node* body = nullptr;
    std::vector<Symbol*> params;
    long frameSize = 0;
    long hiddenRet = -1;
};

struct Node {
    NK kind;
    std::string text;
    std::vector<Node*> kids;
    int line = 0, col = 0, eline = 0, ecol = 0;
    Type* type = nullptr;
    Symbol* sym = nullptr;
    Op op = Op::None;
    long ival = 0;
    double dval = 0;
    long scale = 1;
    Type* opType = nullptr;
    int lo = 0, hi = 0;
    long tempOffset = -1;
    bool resolved = false;
    std::vector<InitOp> inits;
    std::vector<Node*> cases;
    Node* target = nullptr;
    FuncInfo* fn = nullptr;
};

struct Reloc {
    long offset;
    Symbol* sym;
    int strId;
    long addend;
};

struct GlobalObj {
    Symbol* sym;
    std::vector<uint8_t> bytes;
    std::vector<Reloc> relocs;
};

struct Program {
    Node* root = nullptr;
    std::vector<GlobalObj*> globals;
    std::vector<std::string> strings;
    std::vector<FuncInfo*> functions;
    std::vector<Symbol*> funcSyms;
};

struct Diag {
    int line, col;
    bool error;
    std::string msg;
};

struct TokenRec {
    std::string kind, text;
    int line, col;
};

extern std::vector<Diag> diags;
extern std::vector<TokenRec> tokenLog;
extern Node* parseRoot;

void addError(int line, int col, const std::string& msg);
void addWarning(int line, int col, const std::string& msg);
bool hasErrors();

Node* mkNode(NK k, int line, int col, int eline, int ecol, std::initializer_list<Node*> kids = {},
             const std::string& text = "");
Op binaryOpFrom(const std::string& s);

void scopePush();
void scopePop();
void declareTypedef(const std::string& name);
bool isTypedefName(const std::string& name);
void collectTypedefs(Node* declaration);

void analyze(Program& prog);
std::string astToJson(Node* root);
std::string astToText(Node* root);
std::string jsonEscape(const std::string& s);
std::string generateAsm(Program& prog);

struct RunResult {
    int exitCode = 0;
    bool failed = false;
    std::string error;
    int errorLine = 0;
    std::string captured;
    long steps = 0;
};

RunResult interpret(Program& prog, long maxSteps, bool capture);
