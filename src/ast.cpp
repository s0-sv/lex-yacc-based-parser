#include "ast.hpp"

#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

static Type* basic(TK k, bool uns = false) {
    Type* t = new Type();
    t->k = k;
    t->uns = uns;
    return t;
}

Type* tyVoid = basic(TK::Void);
Type* tyBool = basic(TK::Bool, true);
Type* tyChar = basic(TK::Char);
Type* tyUChar = basic(TK::Char, true);
Type* tyShort = basic(TK::Short);
Type* tyUShort = basic(TK::Short, true);
Type* tyInt = basic(TK::Int);
Type* tyUInt = basic(TK::Int, true);
Type* tyLong = basic(TK::Long);
Type* tyULong = basic(TK::Long, true);
Type* tyFloat = basic(TK::Float);
Type* tyDouble = basic(TK::Double);

std::vector<Diag> diags;
std::vector<TokenRec> tokenLog;
Node* parseRoot = nullptr;

Type* mkPtr(Type* base) {
    Type* t = basic(TK::Ptr, true);
    t->base = base;
    return t;
}

Type* mkArray(Type* base, long len) {
    Type* t = basic(TK::Array);
    t->base = base;
    t->len = len;
    return t;
}

long sizeOf(Type* t) {
    switch (t->k) {
        case TK::Void: return 1;
        case TK::Bool:
        case TK::Char: return 1;
        case TK::Short: return 2;
        case TK::Int:
        case TK::Float: return 4;
        case TK::Long:
        case TK::Double:
        case TK::Ptr: return 8;
        case TK::Array: return t->len < 0 ? 0 : t->len * sizeOf(t->base);
        case TK::Struct: return t->st->size;
        case TK::Func: return 1;
    }
    return 0;
}

long alignOf(Type* t) {
    switch (t->k) {
        case TK::Array: return alignOf(t->base);
        case TK::Struct: return t->st->align;
        case TK::Func:
        case TK::Void: return 1;
        default: return sizeOf(t);
    }
}

bool isInt(Type* t) {
    return t->k == TK::Bool || t->k == TK::Char || t->k == TK::Short || t->k == TK::Int || t->k == TK::Long;
}

bool isFp(Type* t) {
    return t->k == TK::Float || t->k == TK::Double;
}

bool isArith(Type* t) {
    return isInt(t) || isFp(t);
}

bool isPtr(Type* t) {
    return t->k == TK::Ptr;
}

bool isScalar(Type* t) {
    return isArith(t) || isPtr(t);
}

bool isAgg(Type* t) {
    return t->k == TK::Array || t->k == TK::Struct;
}

bool sameType(Type* a, Type* b) {
    if (a == b) return true;
    if (a->k != b->k) return false;
    switch (a->k) {
        case TK::Ptr: return sameType(a->base, b->base);
        case TK::Array: return a->len == b->len && sameType(a->base, b->base);
        case TK::Struct: return a->st == b->st;
        case TK::Func:
            if (!sameType(a->base, b->base) || a->variadic != b->variadic) return false;
            if (a->noproto || b->noproto) return true;
            if (a->params.size() != b->params.size()) return false;
            for (size_t i = 0; i < a->params.size(); i++) {
                if (!sameType(a->params[i], b->params[i])) return false;
            }
            return true;
        default: return a->uns == b->uns;
    }
}

std::string typeStr(Type* t, const std::string& inner) {
    if (!t) return "?";
    switch (t->k) {
        case TK::Ptr: {
            std::string in = "*" + inner;
            if (t->base->k == TK::Array || t->base->k == TK::Func) in = "(" + in + ")";
            return typeStr(t->base, in);
        }
        case TK::Array:
            return typeStr(t->base, inner + "[" + (t->len < 0 ? "" : std::to_string(t->len)) + "]");
        case TK::Func: {
            std::string p = "(";
            for (size_t i = 0; i < t->params.size(); i++) {
                if (i) p += ", ";
                p += typeStr(t->params[i]);
            }
            if (t->variadic) p += t->params.empty() ? "..." : ", ...";
            if (t->params.empty() && !t->variadic && !t->noproto) p += "void";
            p += ")";
            return typeStr(t->base, inner + p);
        }
        default: break;
    }
    std::string name;
    switch (t->k) {
        case TK::Void: name = "void"; break;
        case TK::Bool: name = "_Bool"; break;
        case TK::Char: name = t->uns ? "unsigned char" : "char"; break;
        case TK::Short: name = t->uns ? "unsigned short" : "short"; break;
        case TK::Int: name = t->uns ? "unsigned int" : "int"; break;
        case TK::Long: name = t->uns ? "unsigned long" : "long"; break;
        case TK::Float: name = "float"; break;
        case TK::Double: name = "double"; break;
        case TK::Struct:
            name = std::string(t->st->isUnion ? "union " : "struct ") + (t->st->tag.empty() ? "<anon>" : t->st->tag);
            break;
        default: break;
    }
    return inner.empty() ? name : name + " " + inner;
}

bool isCompare(Op op) {
    return op == Op::Lt || op == Op::Le || op == Op::Gt || op == Op::Ge || op == Op::Eq || op == Op::Ne;
}

long normInt(Type* t, long v) {
    switch (t->k) {
        case TK::Bool: return v != 0;
        case TK::Char: return t->uns ? (long)(uint8_t)v : (long)(int8_t)v;
        case TK::Short: return t->uns ? (long)(uint16_t)v : (long)(int16_t)v;
        case TK::Int: return t->uns ? (long)(uint32_t)v : (long)(int32_t)v;
        default: return v;
    }
}

long intBinary(Op op, Type* t, long a, long b, bool* divZero) {
    unsigned long ua = (unsigned long)a, ub = (unsigned long)b;
    long r = 0;
    switch (op) {
        case Op::Add: r = (long)(ua + ub); break;
        case Op::Sub: r = (long)(ua - ub); break;
        case Op::Mul: r = (long)(ua * ub); break;
        case Op::Div:
            if (b == 0) {
                if (divZero) *divZero = true;
                return 0;
            }
            if (t->uns) r = (long)(ua / ub);
            else if (b == -1) r = (long)(0UL - ua);
            else r = a / b;
            break;
        case Op::Mod:
            if (b == 0) {
                if (divZero) *divZero = true;
                return 0;
            }
            if (t->uns) r = (long)(ua % ub);
            else if (b == -1) r = 0;
            else r = a % b;
            break;
        case Op::Shl: r = (long)(ua << (b & 63)); break;
        case Op::Shr: r = t->uns ? (long)(ua >> (b & 63)) : a >> (b & 63); break;
        case Op::BitAnd: r = a & b; break;
        case Op::BitOr: r = a | b; break;
        case Op::BitXor: r = a ^ b; break;
        default: break;
    }
    return normInt(t, r);
}

double fpBinary(Op op, bool single, double a, double b) {
    double r = 0;
    switch (op) {
        case Op::Add: r = single ? (float)a + (float)b : a + b; break;
        case Op::Sub: r = single ? (float)a - (float)b : a - b; break;
        case Op::Mul: r = single ? (float)a * (float)b : a * b; break;
        case Op::Div: r = single ? (float)a / (float)b : a / b; break;
        default: break;
    }
    return single ? (double)(float)r : r;
}

long fpCompare(Op op, double a, double b) {
    switch (op) {
        case Op::Lt: return a < b;
        case Op::Le: return a <= b;
        case Op::Gt: return a > b;
        case Op::Ge: return a >= b;
        case Op::Eq: return a == b;
        case Op::Ne: return a != b;
        default: return 0;
    }
}

long intCompare(Op op, bool uns, long a, long b) {
    unsigned long ua = (unsigned long)a, ub = (unsigned long)b;
    switch (op) {
        case Op::Lt: return uns ? ua < ub : a < b;
        case Op::Le: return uns ? ua <= ub : a <= b;
        case Op::Gt: return uns ? ua > ub : a > b;
        case Op::Ge: return uns ? ua >= ub : a >= b;
        case Op::Eq: return a == b;
        case Op::Ne: return a != b;
        default: return 0;
    }
}

double castToFp(Type* from, Type* to, long i, double d) {
    double r;
    if (isFp(from)) r = d;
    else if (to->k == TK::Float) r = from->uns ? (double)(float)(unsigned long)i : (double)(float)i;
    else r = from->uns ? (double)(unsigned long)i : (double)i;
    return to->k == TK::Float ? (double)(float)r : r;
}

long castToInt(Type* from, Type* to, long i, double d) {
    if (to->k == TK::Bool) return isFp(from) ? d != 0 : i != 0;
    if (!isFp(from)) return normInt(to, i);
    if (std::isnan(d)) return normInt(to, 0);
    if (to->k == TK::Long && to->uns) {
        if (d < 0) return (long)d;
        return (long)(unsigned long)d;
    }
    if (d >= 9.3e18 || d <= -9.3e18) return normInt(to, 0);
    return normInt(to, (long)d);
}

void addError(int line, int col, const std::string& msg) {
    diags.push_back({line, col, true, msg});
}

void addWarning(int line, int col, const std::string& msg) {
    diags.push_back({line, col, false, msg});
}

bool hasErrors() {
    for (const auto& d : diags) {
        if (d.error) return true;
    }
    return false;
}

Node* mkNode(NK k, int line, int col, int eline, int ecol, std::initializer_list<Node*> kids, const std::string& text) {
    Node* n = new Node();
    n->kind = k;
    n->line = line;
    n->col = col;
    n->eline = eline;
    n->ecol = ecol;
    n->kids = kids;
    n->text = text;
    return n;
}

Op binaryOpFrom(const std::string& s) {
    static const std::map<std::string, Op> table = {
        {"+", Op::Add}, {"-", Op::Sub}, {"*", Op::Mul}, {"/", Op::Div}, {"%", Op::Mod},
        {"<<", Op::Shl}, {">>", Op::Shr}, {"&", Op::BitAnd}, {"|", Op::BitOr}, {"^", Op::BitXor},
        {"<", Op::Lt}, {"<=", Op::Le}, {">", Op::Gt}, {">=", Op::Ge}, {"==", Op::Eq}, {"!=", Op::Ne},
        {"&&", Op::LAnd}, {"||", Op::LOr},
    };
    return table.at(s);
}

static std::vector<std::set<std::string>> typedefScopes = {
    {"size_t", "ssize_t", "ptrdiff_t", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t"},
    {},
};

void scopePush() {
    typedefScopes.emplace_back();
}

void scopePop() {
    typedefScopes.pop_back();
}

void declareTypedef(const std::string& name) {
    typedefScopes.back().insert(name);
}

bool isTypedefName(const std::string& name) {
    for (const auto& s : typedefScopes) {
        if (s.count(name)) return true;
    }
    return false;
}

static const std::string* declaratorName(Node* d) {
    while (d) {
        if (d->kind == NK::DeclName) return &d->text;
        d = d->kids[0];
    }
    return nullptr;
}

void collectTypedefs(Node* declaration) {
    Node* specs = declaration->kids[0];
    bool isTypedef = false;
    for (Node* s : specs->kids) {
        if (s->kind == NK::Spec && s->text == "typedef") isTypedef = true;
    }
    if (!isTypedef) return;
    for (size_t i = 1; i < declaration->kids.size(); i++) {
        const std::string* name = declaratorName(declaration->kids[i]->kids[0]);
        if (name) declareTypedef(*name);
    }
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back((char)c);
                }
        }
    }
    return out;
}

static std::string cEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case 0: out += "\\0"; break;
            default: out.push_back((char)c);
        }
    }
    return out;
}

static const char* kindName(NK k) {
    switch (k) {
        case NK::TranslationUnit: return "TranslationUnit";
        case NK::FunctionDef: return "FunctionDef";
        case NK::Declaration: return "Declaration";
        case NK::VarDecl: return "VarDecl";
        case NK::ParamDecl: return "ParamDecl";
        case NK::Specifiers: return "Specifiers";
        case NK::Spec: return "Spec";
        case NK::TypedefRef: return "TypedefRef";
        case NK::StructSpec: return "StructSpec";
        case NK::EnumSpec: return "EnumSpec";
        case NK::Enumerator: return "Enumerator";
        case NK::FieldDecl: return "FieldDecl";
        case NK::FieldDeclarator: return "Field";
        case NK::DeclName: return "DeclName";
        case NK::PointerDecl: return "PointerDecl";
        case NK::ArrayDecl: return "ArrayDecl";
        case NK::FuncDecl: return "FuncDecl";
        case NK::TypeName: return "TypeName";
        case NK::InitList: return "InitList";
        case NK::Designated: return "Designated";
        case NK::DesigField: return "DesigField";
        case NK::DesigIndex: return "DesigIndex";
        case NK::Block: return "Block";
        case NK::ExprStmt: return "ExprStmt";
        case NK::If: return "If";
        case NK::While: return "While";
        case NK::DoWhile: return "DoWhile";
        case NK::For: return "For";
        case NK::Switch: return "Switch";
        case NK::Case: return "Case";
        case NK::Default: return "Default";
        case NK::Break: return "Break";
        case NK::Continue: return "Continue";
        case NK::Return: return "Return";
        case NK::Goto: return "Goto";
        case NK::Label: return "Label";
        case NK::Empty: return "Empty";
        case NK::Ident: return "Ident";
        case NK::IntLit: return "IntLit";
        case NK::FloatLit: return "FloatLit";
        case NK::CharLit: return "CharLit";
        case NK::StrLit: return "StrLit";
        case NK::Binary: return "Binary";
        case NK::Assign: return "Assign";
        case NK::CompoundAssign: return "CompoundAssign";
        case NK::Unary: return "Unary";
        case NK::AddrOf: return "AddrOf";
        case NK::Deref: return "Deref";
        case NK::PreInc: return "PreInc";
        case NK::PreDec: return "PreDec";
        case NK::PostInc: return "PostInc";
        case NK::PostDec: return "PostDec";
        case NK::Cast: return "Cast";
        case NK::ImplicitCast: return "ImplicitCast";
        case NK::SizeofExpr: return "SizeofExpr";
        case NK::SizeofType: return "SizeofType";
        case NK::Call: return "Call";
        case NK::Index: return "Index";
        case NK::Member: return "Member";
        case NK::Ternary: return "Ternary";
        case NK::Comma: return "Comma";
    }
    return "?";
}

static const char* categoryOf(NK k) {
    switch (k) {
        case NK::TranslationUnit: return "root";
        case NK::FunctionDef:
        case NK::Declaration:
        case NK::VarDecl:
        case NK::ParamDecl:
        case NK::Enumerator:
        case NK::FieldDecl:
        case NK::FieldDeclarator:
        case NK::InitList:
        case NK::Designated:
        case NK::DesigField:
        case NK::DesigIndex: return "decl";
        case NK::Specifiers:
        case NK::Spec:
        case NK::TypedefRef:
        case NK::StructSpec:
        case NK::EnumSpec:
        case NK::DeclName:
        case NK::PointerDecl:
        case NK::ArrayDecl:
        case NK::FuncDecl:
        case NK::TypeName: return "type";
        case NK::Block:
        case NK::ExprStmt:
        case NK::If:
        case NK::While:
        case NK::DoWhile:
        case NK::For:
        case NK::Switch:
        case NK::Case:
        case NK::Default:
        case NK::Break:
        case NK::Continue:
        case NK::Return:
        case NK::Goto:
        case NK::Label:
        case NK::Empty: return "stmt";
        case NK::Ident:
        case NK::IntLit:
        case NK::FloatLit:
        case NK::CharLit:
        case NK::StrLit: return "leaf";
        case NK::ImplicitCast: return "implicit";
        default: return "expr";
    }
}

static std::string labelOf(Node* n) {
    switch (n->kind) {
        case NK::TranslationUnit: return "program";
        case NK::FunctionDef: return n->sym ? n->sym->name + "()" : "function";
        case NK::Declaration: return "declaration";
        case NK::VarDecl: return n->sym ? n->sym->name : "declarator";
        case NK::ParamDecl: return n->text.empty() ? "param" : n->text;
        case NK::Specifiers: return "specifiers";
        case NK::StructSpec: return std::string(n->ival ? "union " : "struct ") + (n->text.empty() ? "<anon>" : n->text);
        case NK::EnumSpec: return "enum " + (n->text.empty() ? std::string("<anon>") : n->text);
        case NK::FieldDecl: return "fields";
        case NK::PointerDecl: return "*";
        case NK::ArrayDecl: return "[ ]";
        case NK::FuncDecl: return "( )";
        case NK::TypeName: return n->type ? typeStr(n->type) : "type";
        case NK::InitList: return "{ }";
        case NK::Designated: return "designated";
        case NK::DesigIndex: return "[ ]";
        case NK::Block: return "{ }";
        case NK::ExprStmt: return "expr;";
        case NK::If: return "if";
        case NK::While: return "while";
        case NK::DoWhile: return "do-while";
        case NK::For: return "for";
        case NK::Switch: return "switch";
        case NK::Case: return "case";
        case NK::Default: return "default";
        case NK::Break: return "break";
        case NK::Continue: return "continue";
        case NK::Return: return "return";
        case NK::Goto: return "goto " + n->text;
        case NK::Label: return n->text + ":";
        case NK::Empty: return ";";
        case NK::StrLit: return "\"" + cEscape(n->text) + "\"";
        case NK::PreInc:
        case NK::PreDec: return n->text + "x";
        case NK::PostInc:
        case NK::PostDec: return "x" + n->text;
        case NK::Cast: return "(" + typeStr(n->type) + ")";
        case NK::ImplicitCast: return "→ " + typeStr(n->type);
        case NK::SizeofExpr:
        case NK::SizeofType: return "sizeof";
        case NK::Call: return "call";
        case NK::Index: return "[ ]";
        case NK::Member: return (n->scale ? "->" : ".") + n->text;
        case NK::Ternary: return "? :";
        default: return n->text;
    }
}

static std::string roleOf(Node* parent, size_t idx) {
    switch (parent->kind) {
        case NK::If: return idx == 0 ? "cond" : idx == 1 ? "then" : "else";
        case NK::While: return idx == 0 ? "cond" : "body";
        case NK::DoWhile: return idx == 0 ? "body" : "cond";
        case NK::For: return idx == 0 ? "init" : idx == 1 ? "cond" : idx == 2 ? "step" : "body";
        case NK::Switch: return idx == 0 ? "value" : "body";
        case NK::Case: return idx == 0 ? "value" : "stmt";
        case NK::Binary:
        case NK::Assign:
        case NK::CompoundAssign: return idx == 0 ? "lhs" : "rhs";
        case NK::Ternary: return idx == 0 ? "cond" : idx == 1 ? "then" : "else";
        case NK::Call: return idx == 0 ? "callee" : "arg" + std::to_string(idx - 1);
        case NK::Index: return idx == 0 ? "base" : "index";
        case NK::VarDecl: return idx == 1 ? "init" : "";
        case NK::FunctionDef: return idx == 2 ? "body" : "";
        default: return "";
    }
}

static bool specifiersHidden(Node* n) {
    return n->kind == NK::Specifiers && n->resolved;
}

static std::vector<std::pair<Node*, std::string>> visibleKids(Node* n) {
    std::vector<std::pair<Node*, std::string>> out;
    for (size_t i = 0; i < n->kids.size(); i++) {
        Node* k = n->kids[i];
        if (!k) continue;
        std::string role = roleOf(n, i);
        if (specifiersHidden(k)) {
            for (Node* s : k->kids) {
                bool defines = (s->kind == NK::StructSpec && s->scale == 1) || (s->kind == NK::EnumSpec && !s->kids.empty());
                if (defines) out.push_back({s, ""});
            }
            continue;
        }
        if (k->resolved && (k->kind == NK::DeclName || k->kind == NK::PointerDecl || k->kind == NK::ArrayDecl || k->kind == NK::FuncDecl)) continue;
        if (k->resolved && k->kind == NK::TypeName) continue;
        out.push_back({k, role});
    }
    return out;
}

static void toJson(Node* n, const std::string& role, std::ostringstream& o) {
    o << "{\"kind\":\"" << kindName(n->kind) << "\",\"cat\":\"" << categoryOf(n->kind) << "\",\"label\":\""
      << jsonEscape(labelOf(n)) << "\",\"line\":" << n->line << ",\"col\":" << n->col << ",\"eline\":" << n->eline
      << ",\"ecol\":" << n->ecol;
    if (!role.empty()) o << ",\"role\":\"" << jsonEscape(role) << "\"";
    if (n->type) o << ",\"type\":\"" << jsonEscape(typeStr(n->type)) << "\"";
    o << ",\"children\":[";
    bool first = true;
    if (n->kind == NK::FunctionDef && n->fn) {
        for (Symbol* p : n->fn->params) {
            if (!first) o << ",";
            first = false;
            o << "{\"kind\":\"ParamDecl\",\"cat\":\"decl\",\"label\":\"" << jsonEscape(p->name) << "\",\"line\":" << p->line
              << ",\"col\":0,\"eline\":" << p->line << ",\"ecol\":0,\"role\":\"param\",\"type\":\""
              << jsonEscape(typeStr(p->type)) << "\",\"children\":[]}";
        }
    }
    for (const auto& kid : visibleKids(n)) {
        if (!first) o << ",";
        first = false;
        toJson(kid.first, kid.second, o);
    }
    o << "]}";
}

std::string astToJson(Node* root) {
    if (!root) return "null";
    std::ostringstream o;
    toJson(root, "", o);
    return o.str();
}

static void toText(Node* n, const std::string& role, const std::string& prefix, bool last, bool isRoot, std::ostringstream& o) {
    o << prefix;
    if (!isRoot) o << (last ? "└─ " : "├─ ");
    if (!role.empty()) o << role << ": ";
    o << kindName(n->kind);
    std::string label = labelOf(n);
    if (!label.empty()) o << " " << label;
    if (n->type) o << " : " << typeStr(n->type);
    o << "\n";
    std::string next = isRoot ? "" : prefix + (last ? "   " : "│  ");
    auto kids = visibleKids(n);
    size_t total = kids.size() + (n->kind == NK::FunctionDef && n->fn ? n->fn->params.size() : 0);
    size_t idx = 0;
    if (n->kind == NK::FunctionDef && n->fn) {
        for (Symbol* p : n->fn->params) {
            idx++;
            o << next << (idx == total ? "└─ " : "├─ ") << "param: ParamDecl " << p->name << " : " << typeStr(p->type) << "\n";
        }
    }
    for (const auto& kid : kids) {
        idx++;
        toText(kid.first, kid.second, next, idx == total, false, o);
    }
}

std::string astToText(Node* root) {
    if (!root) return "";
    std::ostringstream o;
    toText(root, "", "", true, true, o);
    return o.str();
}
