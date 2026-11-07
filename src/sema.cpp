#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>

#include "ast.hpp"

namespace {

struct ConstVal {
    bool ok = false;
    bool fp = false;
    long i = 0;
    double d = 0;
    Symbol* sym = nullptr;
    int strId = -1;
};

struct BuiltinDecl {
    const char* name;
    Type* ret;
    std::vector<Type*> params;
    bool variadic;
};

class Sema {
public:
    explicit Sema(Program& p) : P(p) {
        scopes.emplace_back();
        tags.emplace_back();
        defineTypedef("size_t", tyULong);
        defineTypedef("ssize_t", tyLong);
        defineTypedef("ptrdiff_t", tyLong);
        defineTypedef("int8_t", tyChar);
        defineTypedef("uint8_t", tyUChar);
        defineTypedef("int16_t", tyShort);
        defineTypedef("uint16_t", tyUShort);
        defineTypedef("int32_t", tyInt);
        defineTypedef("uint32_t", tyUInt);
        defineTypedef("int64_t", tyLong);
        defineTypedef("uint64_t", tyULong);
    }

    void run(Node* root) {
        root->resolved = true;
        for (Node* k : root->kids) {
            if (k->kind == NK::FunctionDef) functionDef(k);
            else declaration(k, true);
        }
        for (auto& entry : scopes[0]) {
            Symbol* s = entry.second;
            if (s->kind == SymKind::Func && s->used && !s->fn && !s->builtin) {
                addError(s->line, 1, "функцію '" + s->name + "' оголошено, але не визначено");
            }
        }
    }

private:
    Program& P;
    std::vector<std::map<std::string, Symbol*>> scopes;
    std::vector<std::map<std::string, Type*>> tags;
    FuncInfo* cur = nullptr;
    int loopDepth = 0;
    std::vector<Node*> switches;
    std::map<std::string, Node*> labels;
    std::vector<Node*> gotos;
    int staticCounter = 0;
    int order = 0;

    void err(Node* n, const std::string& m) {
        addError(n->line, n->col, m);
    }

    void warn(Node* n, const std::string& m) {
        addWarning(n->line, n->col, m);
    }

    void defineTypedef(const std::string& name, Type* t) {
        Symbol* s = new Symbol();
        s->name = name;
        s->kind = SymKind::Typedef;
        s->type = t;
        scopes[0][name] = s;
    }

    Symbol* lookup(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        return nullptr;
    }

    Type* lookupTag(const std::string& name) {
        for (auto it = tags.rbegin(); it != tags.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        return nullptr;
    }

    void pushScope() {
        scopes.emplace_back();
        tags.emplace_back();
    }

    void popScope() {
        scopes.pop_back();
        tags.pop_back();
    }

    Symbol* builtin(const std::string& name) {
        Type* cs = mkPtr(tyChar);
        Type* vp = mkPtr(tyVoid);
        static std::vector<BuiltinDecl> table;
        if (table.empty()) {
            table = {
                {"printf", tyInt, {cs}, true},
                {"sprintf", tyInt, {cs, cs}, true},
                {"snprintf", tyInt, {cs, tyULong, cs}, true},
                {"scanf", tyInt, {cs}, true},
                {"puts", tyInt, {cs}, false},
                {"putchar", tyInt, {tyInt}, false},
                {"getchar", tyInt, {}, false},
                {"malloc", vp, {tyULong}, false},
                {"calloc", vp, {tyULong, tyULong}, false},
                {"realloc", vp, {vp, tyULong}, false},
                {"free", tyVoid, {vp}, false},
                {"strlen", tyULong, {cs}, false},
                {"strcmp", tyInt, {cs, cs}, false},
                {"strncmp", tyInt, {cs, cs, tyULong}, false},
                {"strcpy", cs, {cs, cs}, false},
                {"strncpy", cs, {cs, cs, tyULong}, false},
                {"strcat", cs, {cs, cs}, false},
                {"strchr", cs, {cs, tyInt}, false},
                {"memset", vp, {vp, tyInt, tyULong}, false},
                {"memcpy", vp, {vp, vp, tyULong}, false},
                {"memmove", vp, {vp, vp, tyULong}, false},
                {"memcmp", tyInt, {vp, vp, tyULong}, false},
                {"abs", tyInt, {tyInt}, false},
                {"labs", tyLong, {tyLong}, false},
                {"atoi", tyInt, {cs}, false},
                {"atol", tyLong, {cs}, false},
                {"atof", tyDouble, {cs}, false},
                {"exit", tyVoid, {tyInt}, false},
                {"rand", tyInt, {}, false},
                {"srand", tyVoid, {tyUInt}, false},
                {"sqrt", tyDouble, {tyDouble}, false},
                {"sin", tyDouble, {tyDouble}, false},
                {"cos", tyDouble, {tyDouble}, false},
                {"tan", tyDouble, {tyDouble}, false},
                {"atan", tyDouble, {tyDouble}, false},
                {"exp", tyDouble, {tyDouble}, false},
                {"log", tyDouble, {tyDouble}, false},
                {"log10", tyDouble, {tyDouble}, false},
                {"floor", tyDouble, {tyDouble}, false},
                {"ceil", tyDouble, {tyDouble}, false},
                {"fabs", tyDouble, {tyDouble}, false},
                {"pow", tyDouble, {tyDouble, tyDouble}, false},
                {"fmod", tyDouble, {tyDouble, tyDouble}, false},
                {"atan2", tyDouble, {tyDouble, tyDouble}, false},
            };
        }
        for (const auto& b : table) {
            if (name == b.name) {
                Type* f = new Type();
                f->k = TK::Func;
                f->base = b.ret;
                f->params = b.params;
                f->variadic = b.variadic;
                Symbol* s = newFuncSym(name, f);
                s->builtin = true;
                return s;
            }
        }
        return nullptr;
    }

    Symbol* newFuncSym(const std::string& name, Type* t) {
        Symbol* s = new Symbol();
        s->name = name;
        s->label = name;
        s->kind = SymKind::Func;
        s->type = t;
        s->global = true;
        s->id = (int)P.funcSyms.size();
        P.funcSyms.push_back(s);
        scopes[0][name] = s;
        return s;
    }

    bool isBuiltinName(const std::string& name) {
        auto saved = scopes[0].find(name);
        if (saved != scopes[0].end()) return saved->second->builtin;
        Symbol* s = builtin(name);
        return s != nullptr;
    }

    struct Storage {
        bool isTypedef = false, isExtern = false, isStatic = false;
    };

    Type* specifiers(Node* specs, Storage* st) {
        specs->resolved = true;
        int nLong = 0, nShort = 0, nSigned = 0, nUnsigned = 0, nInt = 0, nChar = 0, nFloat = 0, nDouble = 0, nVoid = 0, nBool = 0;
        Type* other = nullptr;
        for (Node* s : specs->kids) {
            if (s->kind == NK::StructSpec) other = structSpec(s);
            else if (s->kind == NK::EnumSpec) other = enumSpec(s);
            else if (s->kind == NK::TypedefRef) {
                Symbol* sym = lookup(s->text);
                if (sym && sym->kind == SymKind::Typedef) other = sym->type;
                else {
                    err(s, "невідомий тип '" + s->text + "'");
                    other = tyInt;
                }
            } else {
                const std::string& t = s->text;
                if (t == "long") nLong++;
                else if (t == "short") nShort++;
                else if (t == "signed") nSigned++;
                else if (t == "unsigned") nUnsigned++;
                else if (t == "int") nInt++;
                else if (t == "char") nChar++;
                else if (t == "float") nFloat++;
                else if (t == "double") nDouble++;
                else if (t == "void") nVoid++;
                else if (t == "_Bool") nBool++;
                else if (t == "typedef" && st) st->isTypedef = true;
                else if (t == "extern" && st) st->isExtern = true;
                else if (t == "static" && st) st->isStatic = true;
            }
        }
        int basics = nLong + nShort + nSigned + nUnsigned + nInt + nChar + nFloat + nDouble + nVoid + nBool;
        if (other) {
            if (basics) err(specs, "некоректне поєднання специфікаторів типу");
            return other;
        }
        if (nSigned && nUnsigned) err(specs, "signed та unsigned не можна поєднувати");
        if (nVoid) return tyVoid;
        if (nBool) return tyBool;
        if (nFloat) return tyFloat;
        if (nDouble) {
            if (nLong) err(specs, "long double не підтримується");
            return tyDouble;
        }
        if (nChar) return nUnsigned ? tyUChar : tyChar;
        if (nShort) return nUnsigned ? tyUShort : tyShort;
        if (nLong) return nUnsigned ? tyULong : tyLong;
        if (basics == 0) err(specs, "у декларації відсутній тип");
        return nUnsigned ? tyUInt : tyInt;
    }

    Type* structSpec(Node* n) {
        bool isDefinition = n->scale == 1;
        Type* t = nullptr;
        if (!n->text.empty()) {
            if (isDefinition) {
                auto f = tags.back().find(n->text);
                if (f != tags.back().end()) t = f->second;
            } else {
                t = lookupTag(n->text);
            }
        }
        if (t && t->k != TK::Struct) {
            err(n, "тег '" + n->text + "' вже використано для іншого типу");
            t = nullptr;
        }
        if (!t) {
            t = new Type();
            t->k = TK::Struct;
            t->st = new StructInfo();
            t->st->tag = n->text;
            t->st->isUnion = n->ival != 0;
            if (!n->text.empty()) tags.back()[n->text] = t;
        }
        n->type = t;
        if (!isDefinition) return t;
        if (t->st->complete) {
            err(n, "повторне визначення '" + typeStr(t) + "'");
            return t;
        }
        StructInfo* si = t->st;
        long off = 0;
        long maxAlign = 1, maxSize = 0;
        auto addField = [&](const std::string& name, Type* ft, Node* where) {
            for (const auto& f : si->fields) {
                if (!name.empty() && f.name == name) err(where, "повторне поле '" + name + "'");
            }
            long a = alignOf(ft);
            long o = si->isUnion ? 0 : (off + a - 1) / a * a;
            if (ft->k == TK::Struct && name.empty()) {
                for (const auto& sub : ft->st->fields) si->fields.push_back({sub.name, sub.type, o + sub.offset});
            } else {
                si->fields.push_back({name, ft, o});
            }
            off = o + sizeOf(ft);
            if (a > maxAlign) maxAlign = a;
            if (sizeOf(ft) > maxSize) maxSize = sizeOf(ft);
        };
        for (Node* fd : n->kids) {
            Type* base = specifiers(fd->kids[0], nullptr);
            if (fd->kids.size() == 1) {
                if (base->k == TK::Struct && base->st->tag.empty() && base->st->complete) addField("", base, fd);
                else if (base->k != TK::Struct) warn(fd, "декларація поля без імені");
                continue;
            }
            for (size_t i = 1; i < fd->kids.size(); i++) {
                Node* d = fd->kids[i];
                if (d->kids[1]) {
                    err(d, "бітові поля не підтримуються");
                    d->resolved = true;
                    continue;
                }
                std::string name;
                Node* fnDecl = nullptr;
                Type* ft = applyDeclarator(base, d->kids[0], name, fnDecl);
                if (ft->k == TK::Func || ft->k == TK::Void || incomplete(ft)) {
                    err(d, "поле '" + name + "' має неповний або некоректний тип");
                    continue;
                }
                d->type = ft;
                d->text = name;
                addField(name, ft, d);
            }
        }
        si->align = maxAlign;
        long raw = si->isUnion ? maxSize : off;
        si->size = (raw + maxAlign - 1) / maxAlign * maxAlign;
        si->complete = true;
        return t;
    }

    Type* enumSpec(Node* n) {
        long next = 0;
        n->type = tyInt;
        for (Node* e : n->kids) {
            if (e->kids[0]) {
                e->kids[0] = expr(e->kids[0]);
                ConstVal v = constEval(e->kids[0]);
                if (!v.ok || v.fp || v.sym || v.strId >= 0) err(e, "значення переліку повинно бути цілою константою");
                else next = v.i;
            }
            if (scopes.back().count(e->text)) err(e, "повторне оголошення '" + e->text + "'");
            Symbol* s = new Symbol();
            s->name = e->text;
            s->kind = SymKind::EnumConst;
            s->type = tyInt;
            s->value = next;
            scopes.back()[e->text] = s;
            e->type = tyInt;
            e->ival = next;
            next++;
        }
        return tyInt;
    }

    bool incomplete(Type* t) {
        if (t->k == TK::Struct) return !t->st->complete;
        if (t->k == TK::Array) return t->len < 0 || incomplete(t->base);
        return false;
    }

    Type* funcType(Type* ret, Node* d) {
        Type* f = new Type();
        f->k = TK::Func;
        f->base = ret;
        f->variadic = d->ival != 0;
        if (ret->k == TK::Array || ret->k == TK::Func) err(d, "функція не може повертати масив або функцію");
        if (d->kids.size() == 1) {
            f->noproto = true;
            return f;
        }
        for (size_t i = 1; i < d->kids.size(); i++) {
            Node* p = d->kids[i];
            Type* base = specifiers(p->kids[0], nullptr);
            std::string name;
            Node* inner = nullptr;
            Type* t = applyDeclarator(base, p->kids[1], name, inner);
            if (t->k == TK::Void && d->kids.size() == 2 && name.empty()) {
                p->type = tyVoid;
                return f;
            }
            if (t->k == TK::Void) err(p, "параметр не може мати тип void");
            if (t->k == TK::Array) t = mkPtr(t->base);
            else if (t->k == TK::Func) t = mkPtr(t);
            p->type = t;
            p->text = name;
            f->params.push_back(t);
        }
        return f;
    }

    Type* applyDeclarator(Type* base, Node* d, std::string& name, Node*& fnDecl) {
        while (d) {
            d->resolved = true;
            switch (d->kind) {
                case NK::DeclName:
                    name = d->text;
                    return base;
                case NK::PointerDecl:
                    base = mkPtr(base);
                    break;
                case NK::ArrayDecl: {
                    long len = -1;
                    if (base->k == TK::Func || base->k == TK::Void) err(d, "масив некоректних елементів");
                    if (d->kids[1]) {
                        d->kids[1] = expr(d->kids[1]);
                        ConstVal v = constEval(d->kids[1]);
                        if (!v.ok || v.fp || v.sym || v.strId >= 0) {
                            err(d, "розмір масиву повинен бути цілою константою (VLA не підтримуються)");
                            len = 1;
                        } else if (v.i <= 0) {
                            err(d, "розмір масиву повинен бути додатним");
                            len = 1;
                        } else {
                            len = v.i;
                        }
                    }
                    if (base->k == TK::Array && base->len < 0) err(d, "внутрішній вимір масиву повинен бути заданий");
                    base = mkArray(base, len);
                    break;
                }
                case NK::FuncDecl:
                    base = funcType(base, d);
                    fnDecl = d;
                    break;
                default:
                    break;
            }
            d = d->kids[0];
        }
        return base;
    }

    Type* typeName(Node* n) {
        n->resolved = true;
        Type* base = specifiers(n->kids[0], nullptr);
        std::string name;
        Node* fn = nullptr;
        Type* t = applyDeclarator(base, n->kids[1], name, fn);
        n->type = t;
        return t;
    }

    long allocLocal(Type* t) {
        long a = alignOf(t);
        if (a < 1) a = 1;
        long off = (cur->frameSize + a - 1) / a * a;
        cur->frameSize = off + sizeOf(t);
        return off;
    }

    Symbol* newVar(const std::string& name, Type* t, bool global, int line) {
        Symbol* s = new Symbol();
        s->name = name;
        s->label = name;
        s->type = t;
        s->global = global;
        s->line = line;
        return s;
    }

    void declareFunc(Node* n, const std::string& name, Type* t) {
        auto f = scopes[0].find(name);
        Symbol* s = f == scopes[0].end() ? nullptr : f->second;
        if (s && s->kind != SymKind::Func) {
            err(n, "'" + name + "' вже оголошено не як функцію");
            return;
        }
        if (s && !sameType(s->type, t)) {
            if (!s->builtin) {
                err(n, "конфліктні оголошення функції '" + name + "'");
                return;
            }
            warn(n, "оголошення '" + name + "' відрізняється від вбудованого");
        }
        if (!s) {
            if (isBuiltinName(name)) {
                s = scopes[0][name];
                if (!sameType(s->type, t)) warn(n, "оголошення '" + name + "' відрізняється від вбудованого");
            } else {
                s = newFuncSym(name, t);
            }
        }
        if (!s->builtin) s->type = t;
        s->line = n->line;
        n->sym = s;
        n->type = s->type;
        if (scopes.size() > 1) scopes.back()[name] = s;
    }

    void declaration(Node* n, bool global) {
        Storage st;
        Type* base = specifiers(n->kids[0], &st);
        for (size_t i = 1; i < n->kids.size(); i++) varDecl(n->kids[i], base, st, global);
    }

    void varDecl(Node* n, Type* base, const Storage& st, bool global) {
        std::string name;
        Node* fnDecl = nullptr;
        Type* t = applyDeclarator(base, n->kids[0], name, fnDecl);
        Node* init = n->kids[1];

        if (st.isTypedef) {
            if (init) err(n, "typedef не може мати ініціалізатор");
            auto f = scopes.back().find(name);
            if (f != scopes.back().end() && (f->second->kind != SymKind::Typedef || !sameType(f->second->type, t))) {
                err(n, "конфліктне оголошення typedef '" + name + "'");
            }
            Symbol* s = new Symbol();
            s->name = name;
            s->kind = SymKind::Typedef;
            s->type = t;
            scopes.back()[name] = s;
            n->sym = s;
            n->type = t;
            n->text = "typedef";
            return;
        }

        if (t->k == TK::Func) {
            if (init) err(n, "функцію не можна ініціалізувати");
            declareFunc(n, name, t);
            return;
        }
        if (t->k == TK::Void) {
            err(n, "змінна '" + name + "' не може мати тип void");
            t = tyInt;
        }
        if (global) globalVar(n, name, t, init, st);
        else localVar(n, name, t, init, st);
    }

    void globalVar(Node* n, const std::string& name, Type* t, Node* init, const Storage& st) {
        auto f = scopes[0].find(name);
        Symbol* s = f == scopes[0].end() ? nullptr : f->second;
        if (s) {
            bool compatible = s->kind == SymKind::Var && (sameType(s->type, t) || (s->type->k == TK::Array && t->k == TK::Array && sameType(s->type->base, t->base) && (s->type->len < 0 || t->len < 0)));
            if (!compatible) {
                err(n, "конфліктні оголошення '" + name + "'");
                return;
            }
            if (t->k == TK::Array && t->len >= 0) s->type = t;
        } else {
            s = newVar(name, t, true, n->line);
            s->internal = st.isStatic;
            scopes[0][name] = s;
        }
        n->sym = s;
        if (init) {
            if (s->hasStorage && definedWithInit.count(s)) {
                err(n, "повторне визначення '" + name + "'");
                return;
            }
            definedWithInit.insert(s);
            defineGlobalObject(s, init);
        } else if (!st.isExtern) {
            tentative(s);
        }
        n->type = s->type;
    }

    std::set<Symbol*> definedWithInit;
    std::map<Symbol*, GlobalObj*> objects;

    GlobalObj* objectFor(Symbol* s) {
        auto it = objects.find(s);
        if (it != objects.end()) return it->second;
        GlobalObj* g = new GlobalObj();
        g->sym = s;
        objects[s] = g;
        P.globals.push_back(g);
        s->hasStorage = true;
        return g;
    }

    void tentative(Symbol* s) {
        GlobalObj* g = objectFor(s);
        if (s->type->k == TK::Array && s->type->len < 0) s->type = mkArray(s->type->base, 1);
        if (incomplete(s->type)) {
            addError(s->line, 1, "змінна '" + s->name + "' має неповний тип");
            return;
        }
        if (!definedWithInit.count(s)) g->bytes.assign(sizeOf(s->type), 0);
    }

    void defineGlobalObject(Symbol* s, Node* init) {
        GlobalObj* g = objectFor(s);
        std::vector<InitOp> ops;
        Type* t = s->type;
        initAt(t, 0, init, ops);
        s->type = t;
        if (incomplete(t)) {
            err(init, "змінна '" + s->name + "' має неповний тип");
            return;
        }
        g->bytes.assign(sizeOf(t), 0);
        g->relocs.clear();
        for (const auto& op : ops) writeGlobalInit(g, op);
    }

    void writeBytes(GlobalObj* g, long off, const void* p, long n) {
        if (off + n <= (long)g->bytes.size()) memcpy(&g->bytes[off], p, n);
    }

    void writeGlobalInit(GlobalObj* g, const InitOp& op) {
        if (!op.expr) {
            uint8_t b = (uint8_t)op.byte;
            writeBytes(g, op.offset, &b, 1);
            return;
        }
        ConstVal v = constEval(op.expr);
        if (!v.ok) {
            err(op.expr, "ініціалізатор глобальної змінної повинен бути константою");
            return;
        }
        if (v.sym || v.strId >= 0) {
            if (sizeOf(op.type) != 8) {
                err(op.expr, "адресний ініціалізатор потребує типу вказівника");
                return;
            }
            g->relocs.push_back({op.offset, v.sym, v.strId, v.i});
            return;
        }
        if (op.type->k == TK::Float) {
            float f = (float)(v.fp ? v.d : (double)v.i);
            writeBytes(g, op.offset, &f, 4);
        } else if (op.type->k == TK::Double) {
            double d = v.fp ? v.d : (double)v.i;
            writeBytes(g, op.offset, &d, 8);
        } else {
            long i = v.fp ? (long)v.d : v.i;
            writeBytes(g, op.offset, &i, sizeOf(op.type));
        }
    }

    void localVar(Node* n, const std::string& name, Type* t, Node* init, const Storage& st) {
        auto dup = scopes.back().find(name);
        if (dup != scopes.back().end() && !st.isExtern) {
            err(n, "повторне оголошення '" + name + "'");
        }
        if (st.isExtern) {
            auto f = scopes[0].find(name);
            Symbol* s;
            if (f != scopes[0].end()) {
                s = f->second;
            } else {
                s = newVar(name, t, true, n->line);
                scopes[0][name] = s;
            }
            scopes.back()[name] = s;
            n->sym = s;
            n->type = t;
            if (init) err(n, "локальне extern-оголошення не може мати ініціалізатор");
            return;
        }
        if (st.isStatic) {
            Symbol* s = newVar(name, t, true, n->line);
            s->label = name + "." + std::to_string(++staticCounter);
            s->internal = true;
            scopes.back()[name] = s;
            n->sym = s;
            if (init) {
                definedWithInit.insert(s);
                defineGlobalObject(s, init);
            } else {
                tentative(s);
            }
            n->type = s->type;
            return;
        }
        Symbol* s = newVar(name, t, false, n->line);
        scopes.back()[name] = s;
        n->sym = s;
        if (init) {
            Type* full = t;
            std::vector<InitOp> ops;
            initAt(full, 0, init, ops);
            s->type = full;
            n->inits = ops;
        }
        if (incomplete(s->type)) {
            err(n, "змінна '" + name + "' має неповний тип");
            s->type = tyInt;
        }
        s->offset = allocLocal(s->type);
        n->type = s->type;
    }

    bool isCharArray(Type* t) {
        return t->k == TK::Array && t->base->k == TK::Char;
    }

    void pushString(Type*& t, long off, Node* lit, std::vector<InitOp>& ops) {
        long need = (long)lit->text.size() + 1;
        if (t->len < 0) t = mkArray(t->base, need);
        if ((long)lit->text.size() > t->len) warn(lit, "рядок довший за масив, зайві символи відкинуто");
        lit->type = mkArray(tyChar, need);
        for (long i = 0; i < (long)lit->text.size() && i < t->len; i++) {
            ops.push_back({off + i, tyChar, nullptr, (unsigned char)lit->text[i]});
        }
    }

    void initAt(Type*& t, long off, Node* init, std::vector<InitOp>& ops) {
        if (isCharArray(t)) {
            if (init->kind == NK::StrLit) {
                pushString(t, off, init, ops);
                return;
            }
            if (init->kind == NK::InitList && init->kids.size() == 1 && init->kids[0]->kind == NK::StrLit) {
                pushString(t, off, init->kids[0], ops);
                return;
            }
        }
        if (isAgg(t)) {
            if (init->kind == NK::InitList) {
                initList(t, off, init, ops);
                return;
            }
            if (t->k == TK::Struct) {
                init = rv(expr(init));
                if (init->type->k == TK::Struct && init->type->st == t->st) {
                    ops.push_back({off, t, init, 0});
                    return;
                }
            }
            err(init, "для агрегатного типу потрібен ініціалізатор у фігурних дужках");
            return;
        }
        if (init->kind == NK::InitList) {
            if (init->kids.size() != 1) {
                if (init->kids.empty()) return;
                err(init, "забагато значень у ініціалізаторі скаляра");
            }
            initAt(t, off, init->kids[0], ops);
            return;
        }
        ops.push_back({off, t, convert(rv(expr(init)), t), 0});
    }

    void initList(Type*& t, long off, Node* list, std::vector<InitOp>& ops) {
        list->type = nullptr;
        size_t idx = 0;
        initAggregate(t, off, list, idx, ops, true);
        list->type = t;
        if (idx < list->kids.size()) warn(list->kids[idx], "зайві елементи в ініціалізаторі");
    }

    void designatedWalk(Type*& t, long& off, Node* item, size_t from, std::vector<InitOp>& ops) {
        Type* sub = t;
        long o = off;
        for (size_t i = from; i + 1 < item->kids.size(); i++) {
            Node* d = item->kids[i];
            if (d->kind == NK::DesigField) {
                const Field* f = sub->k == TK::Struct ? findField(sub, d->text) : nullptr;
                if (!f) {
                    err(d, "поле '" + d->text + "' не знайдено");
                    return;
                }
                sub = f->type;
                o += f->offset;
            } else {
                d->kids[0] = expr(d->kids[0]);
                ConstVal v = constEval(d->kids[0]);
                if (sub->k != TK::Array || !v.ok || v.fp) {
                    err(d, "некоректний індексний десигнатор");
                    return;
                }
                sub = sub->base;
                o += v.i * sizeOf(sub);
            }
        }
        initAt(sub, o, item->kids.back(), ops);
    }

    const Field* findField(Type* t, const std::string& name) {
        for (const auto& f : t->st->fields) {
            if (f.name == name) return &f;
        }
        return nullptr;
    }

    void initAggregate(Type*& t, long off, Node* list, size_t& idx, std::vector<InitOp>& ops, bool top) {
        auto& items = list->kids;
        if (t->k == TK::Array) {
            long i = 0, maxI = 0;
            long esz = sizeOf(t->base);
            while (idx < items.size()) {
                Node* item = items[idx];
                if (item->kind == NK::Designated) {
                    if (!top) break;
                    Node* d = item->kids[0];
                    if (d->kind != NK::DesigIndex) {
                        err(d, "для масиву потрібен індексний десигнатор");
                        idx++;
                        continue;
                    }
                    d->kids[0] = expr(d->kids[0]);
                    ConstVal v = constEval(d->kids[0]);
                    if (!v.ok || v.fp || v.i < 0 || (t->len >= 0 && v.i >= t->len)) {
                        err(d, "індекс десигнатора поза межами масиву");
                        idx++;
                        continue;
                    }
                    i = v.i;
                    Type* elem = t->base;
                    long o = off + i * esz;
                    designatedWalk(elem, o, item, 1, ops);
                    idx++;
                    i++;
                    if (i > maxI) maxI = i;
                    continue;
                }
                if (t->len >= 0 && i >= t->len) break;
                Type* elem = t->base;
                initElement(elem, off + i * esz, list, idx, ops);
                i++;
                if (i > maxI) maxI = i;
            }
            if (t->len < 0) t = mkArray(t->base, maxI);
            return;
        }
        StructInfo* si = t->st;
        size_t fi = 0;
        while (idx < items.size()) {
            Node* item = items[idx];
            if (item->kind == NK::Designated) {
                if (!top) break;
                Node* d = item->kids[0];
                bool found = false;
                if (d->kind == NK::DesigField) {
                    for (size_t j = 0; j < si->fields.size(); j++) {
                        if (si->fields[j].name == d->text) {
                            fi = j;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    err(d, "поле десигнатора не знайдено: " + d->text);
                    idx++;
                    continue;
                }
                Type* ft = si->fields[fi].type;
                long o = off + si->fields[fi].offset;
                designatedWalk(ft, o, item, 1, ops);
                idx++;
                fi++;
                if (si->isUnion) fi = si->fields.size();
                continue;
            }
            if (fi >= si->fields.size()) break;
            Type* ft = si->fields[fi].type;
            initElement(ft, off + si->fields[fi].offset, list, idx, ops);
            fi++;
            if (si->isUnion) fi = si->fields.size();
        }
    }

    void initElement(Type*& e, long off, Node* list, size_t& idx, std::vector<InitOp>& ops) {
        Node* item = list->kids[idx];
        if (isCharArray(e) && item->kind == NK::StrLit) {
            pushString(e, off, item, ops);
            idx++;
            return;
        }
        if (isAgg(e)) {
            if (item->kind == NK::InitList) {
                idx++;
                initList(e, off, item, ops);
                return;
            }
            if (e->k == TK::Struct) {
                list->kids[idx] = item = expr(item);
                if (item->type->k == TK::Struct && item->type->st == e->st) {
                    ops.push_back({off, e, item, 0});
                    idx++;
                    return;
                }
            }
            initAggregate(e, off, list, idx, ops, false);
            return;
        }
        idx++;
        initAt(e, off, item, ops);
    }

    void functionDef(Node* n) {
        Storage st;
        Type* base = specifiers(n->kids[0], &st);
        std::string name;
        Node* fnDecl = nullptr;
        Type* t = applyDeclarator(base, n->kids[1], name, fnDecl);
        if (t->k != TK::Func || !fnDecl) {
            err(n, "визначення функції потребує функціонального декларатора");
            return;
        }
        if (t->variadic) err(n, "визначення власних variadic-функцій не підтримується");
        declareFunc(n, name, t);
        Symbol* s = n->sym;
        if (!s) return;
        if (s->fn) {
            err(n, "повторне визначення функції '" + name + "'");
            return;
        }
        if (s->builtin) {
            s->builtin = false;
            s->type = t;
            n->type = t;
        }
        s->internal = st.isStatic;
        if (t->base->k == TK::Struct && !t->base->st->complete) err(n, "функція повертає неповний тип");
        FuncInfo* fi = new FuncInfo();
        fi->sym = s;
        s->fn = fi;
        n->fn = fi;
        cur = fi;
        P.functions.push_back(fi);
        pushScope();
        if (t->base->k == TK::Struct) fi->hiddenRet = allocLocal(mkPtr(tyVoid));
        for (size_t i = 1; i < fnDecl->kids.size(); i++) {
            Node* p = fnDecl->kids[i];
            if (!p->type || p->type->k == TK::Void) continue;
            if (p->text.empty()) {
                err(p, "параметр визначення функції повинен мати ім'я");
                continue;
            }
            if (scopes.back().count(p->text)) err(p, "повторний параметр '" + p->text + "'");
            if (incomplete(p->type)) err(p, "параметр має неповний тип");
            Symbol* ps = newVar(p->text, p->type, false, p->line);
            ps->offset = allocLocal(ps->type);
            scopes.back()[p->text] = ps;
            fi->params.push_back(ps);
        }
        labels.clear();
        gotos.clear();
        loopDepth = 0;
        switches.clear();
        fi->body = n->kids[2];
        block(n->kids[2], false);
        popScope();
        for (Node* g : gotos) {
            auto f = labels.find(g->text);
            if (f == labels.end()) err(g, "мітку '" + g->text + "' не визначено");
            else g->target = f->second;
        }
        order = 0;
        number(n->kids[2]);
        cur = nullptr;
    }

    void number(Node* n) {
        if (!n) return;
        n->lo = ++order;
        for (Node* k : n->kids) number(k);
        n->hi = ++order;
    }

    void block(Node* n, bool newScope) {
        if (newScope) pushScope();
        for (auto& k : n->kids) {
            if (k->kind == NK::Declaration) declaration(k, false);
            else stmt(k);
        }
        if (newScope) popScope();
    }

    Node* condition(Node* e) {
        e = rv(expr(e));
        if (!isScalar(e->type)) err(e, "умова повинна мати скалярний тип");
        return e;
    }

    void stmt(Node* n) {
        switch (n->kind) {
            case NK::Block:
                block(n, true);
                break;
            case NK::ExprStmt:
                n->kids[0] = rv(expr(n->kids[0]));
                break;
            case NK::If:
                n->kids[0] = condition(n->kids[0]);
                stmt(n->kids[1]);
                if (n->kids[2]) stmt(n->kids[2]);
                break;
            case NK::While:
                n->kids[0] = condition(n->kids[0]);
                loopDepth++;
                stmt(n->kids[1]);
                loopDepth--;
                break;
            case NK::DoWhile:
                loopDepth++;
                stmt(n->kids[0]);
                loopDepth--;
                n->kids[1] = condition(n->kids[1]);
                break;
            case NK::For:
                pushScope();
                if (n->kids[0]) {
                    if (n->kids[0]->kind == NK::Declaration) declaration(n->kids[0], false);
                    else n->kids[0] = rv(expr(n->kids[0]));
                }
                if (n->kids[1]) n->kids[1] = condition(n->kids[1]);
                if (n->kids[2]) n->kids[2] = rv(expr(n->kids[2]));
                loopDepth++;
                stmt(n->kids[3]);
                loopDepth--;
                popScope();
                break;
            case NK::Switch: {
                Node* v = rv(expr(n->kids[0]));
                if (!isInt(v->type)) err(n, "вираз switch повинен бути цілочисельним");
                else v = promote(v);
                n->kids[0] = v;
                switches.push_back(n);
                stmt(n->kids[1]);
                switches.pop_back();
                break;
            }
            case NK::Case: {
                if (switches.empty()) {
                    err(n, "case поза switch");
                } else {
                    n->kids[0] = expr(n->kids[0]);
                    ConstVal v = constEval(n->kids[0]);
                    if (!v.ok || v.fp || v.sym || v.strId >= 0) {
                        err(n, "значення case повинно бути цілою константою");
                    } else {
                        Node* sw = switches.back();
                        n->ival = normInt(sw->kids[0]->type, v.i);
                        for (Node* other : sw->cases) {
                            if (other->kind == NK::Case && other->ival == n->ival) err(n, "повторне значення case");
                        }
                        sw->cases.push_back(n);
                    }
                }
                stmt(n->kids[1]);
                break;
            }
            case NK::Default:
                if (switches.empty()) {
                    err(n, "default поза switch");
                } else {
                    Node* sw = switches.back();
                    if (sw->target) err(n, "повторний default у switch");
                    sw->target = n;
                }
                stmt(n->kids[0]);
                break;
            case NK::Break:
                if (loopDepth == 0 && switches.empty()) err(n, "break поза циклом або switch");
                break;
            case NK::Continue:
                if (loopDepth == 0) err(n, "continue поза циклом");
                break;
            case NK::Return: {
                Type* ret = cur->sym->type->base;
                if (n->kids[0]) {
                    Node* e = rv(expr(n->kids[0]));
                    if (ret->k == TK::Void) {
                        if (e->type->k != TK::Void) err(n, "функція void не може повертати значення");
                        n->kids[0] = e;
                    } else {
                        n->kids[0] = convert(e, ret);
                    }
                } else if (ret->k != TK::Void) {
                    warn(n, "return без значення у функції, що повертає " + typeStr(ret));
                }
                n->type = ret;
                break;
            }
            case NK::Goto:
                gotos.push_back(n);
                break;
            case NK::Label:
                if (labels.count(n->text)) err(n, "повторна мітка '" + n->text + "'");
                labels[n->text] = n;
                stmt(n->kids[0]);
                break;
            default:
                break;
        }
    }

    Node* wrapCast(Node* e, Type* t) {
        Node* c = mkNode(NK::ImplicitCast, e->line, e->col, e->eline, e->ecol, {e});
        c->type = t;
        return c;
    }

    Node* rv(Node* e) {
        if (e->type->k == TK::Array) return wrapCast(e, mkPtr(e->type->base));
        if (e->type->k == TK::Func) return wrapCast(e, mkPtr(e->type));
        return e;
    }

    Node* castTo(Node* e, Type* t) {
        if (sameType(e->type, t)) return e;
        return wrapCast(e, t);
    }

    Node* promote(Node* e) {
        if (isInt(e->type) && sizeOf(e->type) < 4) return castTo(e, tyInt);
        return e;
    }

    Type* commonType(Type* a, Type* b) {
        if (a->k == TK::Double || b->k == TK::Double) return tyDouble;
        if (a->k == TK::Float || b->k == TK::Float) return tyFloat;
        long sa = sizeOf(a) < 4 ? 4 : sizeOf(a);
        long sb = sizeOf(b) < 4 ? 4 : sizeOf(b);
        bool ua = sizeOf(a) >= 4 && a->uns;
        bool ub = sizeOf(b) >= 4 && b->uns;
        if (sa > sb) return sa == 8 ? (ua ? tyULong : tyLong) : (ua ? tyUInt : tyInt);
        if (sb > sa) return sb == 8 ? (ub ? tyULong : tyLong) : (ub ? tyUInt : tyInt);
        bool u = ua || ub;
        return sa == 8 ? (u ? tyULong : tyLong) : (u ? tyUInt : tyInt);
    }

    bool isNullConst(Node* e) {
        ConstVal v = constEval(e);
        return v.ok && !v.fp && !v.sym && v.strId < 0 && v.i == 0;
    }

    Node* convert(Node* e, Type* t) {
        Type* from = e->type;
        if (t->k == TK::Struct) {
            if (from->k != TK::Struct || from->st != t->st) err(e, "несумісні типи: " + typeStr(from) + " і " + typeStr(t));
            return e;
        }
        if (t->k == TK::Array) {
            err(e, "масиву не можна присвоювати");
            return e;
        }
        if (from->k == TK::Struct || from->k == TK::Void) {
            err(e, "несумісні типи: " + typeStr(from) + " і " + typeStr(t));
            return e;
        }
        if (isPtr(t)) {
            if (isPtr(from)) {
                bool voidSide = t->base->k == TK::Void || from->base->k == TK::Void;
                if (!voidSide && !sameType(t->base, from->base)) warn(e, "несумісні вказівники: " + typeStr(from) + " і " + typeStr(t));
            } else if (isInt(from)) {
                if (!isNullConst(e)) warn(e, "перетворення цілого на вказівник без приведення");
            } else {
                err(e, "несумісні типи: " + typeStr(from) + " і " + typeStr(t));
                return e;
            }
        } else if (isPtr(from)) {
            if (isFp(t)) {
                err(e, "вказівник не можна перетворити на дійсне число");
                return e;
            }
            if (t->k != TK::Bool) warn(e, "перетворення вказівника на ціле без приведення");
        }
        return castTo(e, t);
    }

    bool isLvalue(Node* e) {
        switch (e->kind) {
            case NK::Ident: return e->sym && e->sym->kind == SymKind::Var;
            case NK::Deref:
            case NK::Index:
            case NK::StrLit: return true;
            case NK::Member: return e->scale == 1 || isLvalue(e->kids[0]);
            default: return false;
        }
    }

    Node* expr(Node* n) {
        if (n->type) return n;
        switch (n->kind) {
            case NK::IntLit: intLit(n); break;
            case NK::FloatLit: floatLit(n); break;
            case NK::CharLit: n->type = tyInt; break;
            case NK::StrLit:
                n->ival = (long)P.strings.size();
                P.strings.push_back(n->text);
                n->type = mkArray(tyChar, (long)n->text.size() + 1);
                break;
            case NK::Ident: ident(n); break;
            case NK::Binary: binary(n); break;
            case NK::Unary: unary(n); break;
            case NK::Assign: assign(n); break;
            case NK::CompoundAssign: compound(n); break;
            case NK::AddrOf: addrOf(n); break;
            case NK::Deref: deref(n); break;
            case NK::PreInc:
            case NK::PreDec:
            case NK::PostInc:
            case NK::PostDec: incDec(n); break;
            case NK::Cast: castExpr(n); break;
            case NK::SizeofExpr: {
                n->kids[0] = expr(n->kids[0]);
                sizeofCheck(n, n->kids[0]->type);
                break;
            }
            case NK::SizeofType: {
                Type* t = typeName(n->kids[0]);
                sizeofCheck(n, t);
                break;
            }
            case NK::Call: call(n); break;
            case NK::Index: index(n); break;
            case NK::Member: member(n); break;
            case NK::Ternary: ternary(n); break;
            case NK::Comma:
                n->kids[0] = rv(expr(n->kids[0]));
                n->kids[1] = rv(expr(n->kids[1]));
                n->type = n->kids[1]->type;
                break;
            default:
                err(n, "неочікуваний вузол у виразі");
                n->type = tyInt;
        }
        return n;
    }

    void sizeofCheck(Node* n, Type* t) {
        if (incomplete(t) || t->k == TK::Func || t->k == TK::Void) err(n, "sizeof застосовано до неповного типу");
        n->ival = sizeOf(t);
        n->type = tyULong;
    }

    void intLit(Node* n) {
        const std::string& s = n->text;
        bool binary = s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B');
        char* end = nullptr;
        unsigned long v = strtoul(binary ? s.c_str() + 2 : s.c_str(), &end, binary ? 2 : 0);
        std::string suffix(end ? end : "");
        bool hasU = false;
        int nL = 0;
        for (char c : suffix) {
            if (c == 'u' || c == 'U') hasU = true;
            else if (c == 'l' || c == 'L') nL++;
            else err(n, "некоректний суфікс числа: " + suffix);
        }
        bool decimal = !binary && !(s[0] == '0' && s.size() > 1);
        Type* t;
        if (hasU) t = (nL || v > 0xFFFFFFFFUL) ? tyULong : tyUInt;
        else if (nL) t = (v > 0x7FFFFFFFFFFFFFFFUL) ? tyULong : tyLong;
        else if (v <= 0x7FFFFFFFUL) t = tyInt;
        else if (!decimal && v <= 0xFFFFFFFFUL) t = tyUInt;
        else if (v <= 0x7FFFFFFFFFFFFFFFUL) t = tyLong;
        else t = tyULong;
        n->ival = normInt(t, (long)v);
        n->type = t;
    }

    void floatLit(Node* n) {
        char* end = nullptr;
        double v = strtod(n->text.c_str(), &end);
        bool single = end && (*end == 'f' || *end == 'F');
        if (end && (*end == 'l' || *end == 'L')) warn(n, "long double трактується як double");
        n->dval = single ? (double)(float)v : v;
        n->type = single ? tyFloat : tyDouble;
    }

    void ident(Node* n) {
        Symbol* s = lookup(n->text);
        if (!s) s = builtin(n->text);
        if (!s) {
            err(n, "ідентифікатор '" + n->text + "' не оголошено");
            n->type = tyInt;
            return;
        }
        if (s->kind == SymKind::Typedef) {
            err(n, "'" + n->text + "' є типом, а не значенням");
            n->type = tyInt;
            return;
        }
        s->used = true;
        n->sym = s;
        n->type = s->type;
    }

    void binary(Node* n) {
        Op op = n->op;
        Node* a = rv(expr(n->kids[0]));
        Node* b = rv(expr(n->kids[1]));
        Type* ta = a->type;
        Type* tb = b->type;
        n->type = tyInt;
        if (op == Op::LAnd || op == Op::LOr) {
            if (!isScalar(ta) || !isScalar(tb)) err(n, "логічні оператори потребують скалярних операндів");
        } else if (isCompare(op)) {
            if (isArith(ta) && isArith(tb)) {
                Type* c = commonType(ta, tb);
                a = castTo(a, c);
                b = castTo(b, c);
            } else if (isPtr(ta) && isPtr(tb)) {
                bool voidSide = ta->base->k == TK::Void || tb->base->k == TK::Void;
                if (!voidSide && !sameType(ta->base, tb->base)) warn(n, "порівняння несумісних вказівників");
            } else if (isPtr(ta) && isInt(tb)) {
                if (!isNullConst(b)) warn(n, "порівняння вказівника з цілим");
                b = wrapCast(b, ta);
            } else if (isInt(ta) && isPtr(tb)) {
                if (!isNullConst(a)) warn(n, "порівняння вказівника з цілим");
                a = wrapCast(a, tb);
            } else {
                err(n, "некоректні операнди порівняння: " + typeStr(ta) + " і " + typeStr(tb));
            }
        } else if ((op == Op::Add || op == Op::Sub) && (isPtr(ta) || isPtr(tb))) {
            if (isPtr(ta) && isPtr(tb)) {
                if (op != Op::Sub) err(n, "вказівники не можна додавати");
                else if (!sameType(ta->base, tb->base)) err(n, "різниця несумісних вказівників");
                n->type = tyLong;
                n->scale = pointee(n, ta);
            } else if (isPtr(ta) && isInt(tb)) {
                b = castTo(b, tyLong);
                n->type = ta;
                n->scale = pointee(n, ta);
            } else if (isInt(ta) && isPtr(tb) && op == Op::Add) {
                Node* tmp = castTo(a, tyLong);
                a = b;
                b = tmp;
                n->type = tb;
                n->scale = pointee(n, tb);
            } else {
                err(n, "некоректні операнди: " + typeStr(ta) + " і " + typeStr(tb));
            }
        } else if (op == Op::Mod || op == Op::Shl || op == Op::Shr || op == Op::BitAnd || op == Op::BitOr || op == Op::BitXor) {
            if (!isInt(ta) || !isInt(tb)) {
                err(n, "оператор '" + n->text + "' потребує цілих операндів");
            } else if (op == Op::Shl || op == Op::Shr) {
                a = promote(a);
                b = castTo(promote(b), tyLong);
                n->type = a->type;
            } else {
                Type* c = commonType(ta, tb);
                a = castTo(a, c);
                b = castTo(b, c);
                n->type = c;
            }
        } else {
            if (!isArith(ta) || !isArith(tb)) {
                err(n, "оператор '" + n->text + "' потребує арифметичних операндів");
            } else {
                Type* c = commonType(ta, tb);
                a = castTo(a, c);
                b = castTo(b, c);
                n->type = c;
            }
        }
        n->kids[0] = a;
        n->kids[1] = b;
    }

    long pointee(Node* n, Type* p) {
        Type* b = p->base;
        if (incomplete(b)) {
            err(n, "арифметика над вказівником на неповний тип");
            return 1;
        }
        return sizeOf(b);
    }

    void unary(Node* n) {
        Node* a = rv(expr(n->kids[0]));
        n->kids[0] = a;
        if (n->op == Op::Not) {
            if (!isScalar(a->type)) err(n, "'!' потребує скалярного операнда");
            n->type = tyInt;
            return;
        }
        if (n->op == Op::BitNot ? !isInt(a->type) : !isArith(a->type)) {
            err(n, "некоректний операнд унарного '" + n->text + "'");
            n->type = tyInt;
            return;
        }
        a = promote(a);
        n->kids[0] = a;
        n->type = a->type;
    }

    void assign(Node* n) {
        Node* l = expr(n->kids[0]);
        n->kids[0] = l;
        Node* r = rv(expr(n->kids[1]));
        if (!isLvalue(l)) err(n, "ліва частина присвоєння не є l-value");
        n->type = l->type;
        if (l->type->k == TK::Array) {
            err(n, "масиву не можна присвоювати");
            n->kids[1] = r;
            return;
        }
        n->kids[1] = convert(r, l->type);
    }

    void compound(Node* n) {
        Node* l = expr(n->kids[0]);
        n->kids[0] = l;
        Node* r = rv(expr(n->kids[1]));
        if (!isLvalue(l)) err(n, "ліва частина присвоєння не є l-value");
        n->type = l->type;
        Op op = n->op;
        if (isPtr(l->type)) {
            if ((op != Op::Add && op != Op::Sub) || !isInt(r->type)) {
                err(n, "некоректні операнди '" + n->text + "' для вказівника");
            } else {
                n->scale = pointee(n, l->type);
                r = castTo(r, tyLong);
            }
            n->opType = l->type;
            n->kids[1] = r;
            return;
        }
        bool intOnly = op == Op::Mod || op == Op::Shl || op == Op::Shr || op == Op::BitAnd || op == Op::BitOr || op == Op::BitXor;
        if (!isArith(l->type) || !isArith(r->type) || (intOnly && (!isInt(l->type) || !isInt(r->type)))) {
            err(n, "некоректні операнди '" + n->text + "'");
            n->opType = tyInt;
            n->kids[1] = r;
            return;
        }
        if (op == Op::Shl || op == Op::Shr) {
            n->opType = sizeOf(l->type) < 4 ? tyInt : l->type;
            n->kids[1] = castTo(r, tyLong);
        } else {
            n->opType = commonType(l->type, r->type);
            n->kids[1] = castTo(r, n->opType);
        }
    }

    void addrOf(Node* n) {
        Node* a = expr(n->kids[0]);
        n->kids[0] = a;
        if (a->type->k != TK::Func && !isLvalue(a)) err(n, "'&' потребує l-value");
        n->type = mkPtr(a->type);
    }

    void deref(Node* n) {
        Node* a = rv(expr(n->kids[0]));
        n->kids[0] = a;
        if (!isPtr(a->type)) {
            err(n, "розіменування не вказівника: " + typeStr(a->type));
            n->type = tyInt;
            return;
        }
        if (a->type->base->k == TK::Void) {
            err(n, "розіменування вказівника на void");
            n->type = tyInt;
            return;
        }
        n->type = a->type->base;
    }

    void incDec(Node* n) {
        Node* a = expr(n->kids[0]);
        n->kids[0] = a;
        if (!isLvalue(a) || !isScalar(a->type)) err(n, "'" + n->text + "' потребує скалярного l-value");
        n->type = a->type;
        n->scale = isPtr(a->type) ? pointee(n, a->type) : 1;
    }

    void castExpr(Node* n) {
        Type* t = typeName(n->kids[0]);
        Node* e = rv(expr(n->kids[1]));
        n->kids[1] = e;
        n->type = t;
        if (t->k == TK::Void) return;
        if (t->k == TK::Struct) {
            if (e->type->k != TK::Struct || e->type->st != t->st) err(n, "некоректне приведення до структури");
            return;
        }
        if (!isScalar(t) || !isScalar(e->type)) err(n, "некоректне приведення типу " + typeStr(e->type) + " до " + typeStr(t));
        if ((isPtr(t) && isFp(e->type)) || (isFp(t) && isPtr(e->type))) err(n, "вказівник і дійсне число не приводяться одне до одного");
    }

    void call(Node* n) {
        Node* callee = expr(n->kids[0]);
        Type* ft = callee->type;
        if (ft->k == TK::Ptr && ft->base->k == TK::Func) {
            ft = ft->base;
        } else if (ft->k != TK::Func) {
            err(n, "виклик не функції: " + typeStr(ft));
            n->type = tyInt;
            for (size_t i = 1; i < n->kids.size(); i++) n->kids[i] = rv(expr(n->kids[i]));
            return;
        }
        n->kids[0] = callee;
        size_t nargs = n->kids.size() - 1;
        size_t np = ft->params.size();
        if (nargs < np || (nargs > np && !ft->variadic && !ft->noproto)) {
            std::string fname = callee->kind == NK::Ident ? "'" + callee->text + "' " : "";
            err(n, "функція " + fname + "очікує " + std::to_string(np) + " аргумент(ів), передано " + std::to_string(nargs));
        }
        for (size_t i = 0; i < nargs; i++) {
            Node* a = rv(expr(n->kids[i + 1]));
            if (i < np) {
                a = convert(a, ft->params[i]);
            } else if (a->type->k == TK::Float) {
                a = wrapCast(a, tyDouble);
            } else if (isInt(a->type)) {
                a = promote(a);
            } else if (a->type->k == TK::Void) {
                err(a, "аргумент не може мати тип void");
            }
            n->kids[i + 1] = a;
        }
        n->type = ft->base;
        if (n->type->k == TK::Struct) {
            if (incomplete(n->type)) err(n, "виклик повертає неповний тип");
            else if (cur) n->tempOffset = allocLocal(n->type);
        }
    }

    void index(Node* n) {
        Node* a = rv(expr(n->kids[0]));
        Node* b = rv(expr(n->kids[1]));
        if (isInt(a->type) && isPtr(b->type)) std::swap(a, b);
        if (!isPtr(a->type) || !isInt(b->type)) {
            err(n, "індексування потребує вказівника/масиву та цілого індексу");
            n->kids[0] = a;
            n->kids[1] = b;
            n->type = tyInt;
            return;
        }
        n->kids[0] = a;
        n->kids[1] = castTo(b, tyLong);
        n->type = a->type->base;
        n->scale = pointee(n, a->type);
    }

    void member(Node* n) {
        Node* a = expr(n->kids[0]);
        Type* st = a->type;
        if (n->scale == 1) {
            a = rv(a);
            if (!isPtr(a->type) || a->type->base->k != TK::Struct) {
                err(n, "оператор '->' потребує вказівника на структуру");
                n->kids[0] = a;
                n->type = tyInt;
                return;
            }
            st = a->type->base;
        } else if (st->k != TK::Struct) {
            err(n, "оператор '.' потребує структури");
            n->type = tyInt;
            return;
        }
        n->kids[0] = a;
        if (!st->st->complete) {
            err(n, "доступ до поля неповного типу " + typeStr(st));
            n->type = tyInt;
            return;
        }
        const Field* f = findField(st, n->text);
        if (!f) {
            err(n, typeStr(st) + " не має поля '" + n->text + "'");
            n->type = tyInt;
            return;
        }
        n->ival = f->offset;
        n->type = f->type;
    }

    void ternary(Node* n) {
        n->kids[0] = condition(n->kids[0]);
        Node* a = rv(expr(n->kids[1]));
        Node* b = rv(expr(n->kids[2]));
        Type* ta = a->type;
        Type* tb = b->type;
        if (isArith(ta) && isArith(tb)) {
            Type* c = commonType(ta, tb);
            a = castTo(a, c);
            b = castTo(b, c);
            n->type = c;
        } else if (isPtr(ta) && isPtr(tb)) {
            if (ta->base->k == TK::Void) n->type = tb;
            else n->type = ta;
            if (ta->base->k == TK::Void) a = wrapCast(a, tb);
            else if (tb->base->k == TK::Void) b = wrapCast(b, ta);
            else if (!sameType(ta, tb)) warn(n, "несумісні вказівники в ?:");
        } else if (isPtr(ta) && isInt(tb) && isNullConst(b)) {
            b = wrapCast(b, ta);
            n->type = ta;
        } else if (isInt(ta) && isPtr(tb) && isNullConst(a)) {
            a = wrapCast(a, tb);
            n->type = tb;
        } else if (ta->k == TK::Void && tb->k == TK::Void) {
            n->type = tyVoid;
        } else if (ta->k == TK::Struct && tb->k == TK::Struct && ta->st == tb->st) {
            n->type = ta;
        } else {
            err(n, "несумісні гілки ?: " + typeStr(ta) + " і " + typeStr(tb));
            n->type = tyInt;
        }
        n->kids[1] = a;
        n->kids[2] = b;
    }

    ConstVal addressOf(Node* n) {
        ConstVal r;
        switch (n->kind) {
            case NK::Ident:
                if (n->sym && n->sym->global && (n->sym->kind == SymKind::Var || n->sym->kind == SymKind::Func)) {
                    r.ok = true;
                    r.sym = n->sym;
                }
                return r;
            case NK::StrLit:
                r.ok = true;
                r.strId = (int)n->ival;
                return r;
            case NK::Index: {
                ConstVal base = constEval(n->kids[0]);
                ConstVal idx = constEval(n->kids[1]);
                if (!base.ok || !idx.ok || idx.fp || idx.sym) return r;
                base.i += idx.i * n->scale;
                return base;
            }
            case NK::Member: {
                ConstVal base = n->scale ? constEval(n->kids[0]) : addressOf(n->kids[0]);
                if (!base.ok) return r;
                base.i += n->ival;
                return base;
            }
            case NK::Deref:
                return constEval(n->kids[0]);
            default:
                return r;
        }
    }

    ConstVal constEval(Node* n) {
        ConstVal r;
        if (!n->type) expr(n);
        switch (n->kind) {
            case NK::IntLit:
            case NK::CharLit:
                r.ok = true;
                r.i = n->ival;
                return r;
            case NK::FloatLit:
                r.ok = true;
                r.fp = true;
                r.d = n->dval;
                return r;
            case NK::SizeofExpr:
            case NK::SizeofType:
                r.ok = true;
                r.i = n->ival;
                return r;
            case NK::Ident:
                if (n->sym && n->sym->kind == SymKind::EnumConst) {
                    r.ok = true;
                    r.i = n->sym->value;
                }
                return r;
            case NK::AddrOf:
                return addressOf(n->kids[0]);
            case NK::Cast:
            case NK::ImplicitCast: {
                Node* inner = n->kind == NK::Cast ? n->kids[1] : n->kids[0];
                Type* from = inner->type;
                Type* to = n->type;
                if (from->k == TK::Array || from->k == TK::Func) return addressOf(inner);
                ConstVal v = constEval(inner);
                if (!v.ok) return r;
                if (v.sym || v.strId >= 0) {
                    if (sizeOf(to) != 8) return r;
                    return v;
                }
                r.ok = true;
                if (isFp(to)) {
                    r.fp = true;
                    r.d = castToFp(v.fp ? tyDouble : from, to, v.i, v.d);
                } else if (isScalar(to)) {
                    r.i = castToInt(v.fp ? tyDouble : from, to, v.i, v.d);
                } else {
                    r.ok = false;
                }
                return r;
            }
            case NK::Unary: {
                ConstVal v = constEval(n->kids[0]);
                if (!v.ok || v.sym || v.strId >= 0) return r;
                r.ok = true;
                switch (n->op) {
                    case Op::Neg:
                        if (v.fp) {
                            r.fp = true;
                            r.d = -v.d;
                        } else {
                            r.i = normInt(n->type, (long)(0UL - (unsigned long)v.i));
                        }
                        break;
                    case Op::Pos:
                        return v;
                    case Op::Not:
                        r.i = v.fp ? v.d == 0 : v.i == 0;
                        break;
                    case Op::BitNot:
                        r.i = normInt(n->type, ~v.i);
                        break;
                    default:
                        r.ok = false;
                }
                return r;
            }
            case NK::Binary: {
                ConstVal a = constEval(n->kids[0]);
                if (!a.ok) return r;
                if (n->op == Op::LAnd || n->op == Op::LOr) {
                    if (a.sym || a.strId >= 0) return r;
                    bool at = a.fp ? a.d != 0 : a.i != 0;
                    if (n->op == Op::LAnd && !at) {
                        r.ok = true;
                        return r;
                    }
                    if (n->op == Op::LOr && at) {
                        r.ok = true;
                        r.i = 1;
                        return r;
                    }
                    ConstVal b = constEval(n->kids[1]);
                    if (!b.ok || b.sym || b.strId >= 0) return r;
                    r.ok = true;
                    r.i = b.fp ? b.d != 0 : b.i != 0;
                    return r;
                }
                ConstVal b = constEval(n->kids[1]);
                if (!b.ok) return r;
                if (a.sym || a.strId >= 0 || b.sym || b.strId >= 0) {
                    if ((n->op == Op::Add || n->op == Op::Sub) && (a.sym || a.strId >= 0) && !b.sym && b.strId < 0 && !b.fp) {
                        a.i += (n->op == Op::Add ? b.i : -b.i) * n->scale;
                        return a;
                    }
                    return r;
                }
                r.ok = true;
                if (isCompare(n->op)) {
                    r.i = (a.fp || b.fp) ? fpCompare(n->op, a.fp ? a.d : (double)a.i, b.fp ? b.d : (double)b.i)
                                         : intCompare(n->op, n->kids[0]->type->uns, a.i, b.i);
                    return r;
                }
                if (isFp(n->type)) {
                    r.fp = true;
                    r.d = fpBinary(n->op, n->type->k == TK::Float, a.fp ? a.d : (double)a.i, b.fp ? b.d : (double)b.i);
                    return r;
                }
                if (isPtr(n->type)) {
                    r.i = n->op == Op::Add ? a.i + b.i * n->scale : a.i - b.i * n->scale;
                    return r;
                }
                bool divZero = false;
                r.i = intBinary(n->op, n->type, a.i, b.i, &divZero);
                if (divZero) {
                    err(n, "ділення на нуль у константному виразі");
                    r.ok = false;
                }
                return r;
            }
            case NK::Ternary: {
                ConstVal c = constEval(n->kids[0]);
                if (!c.ok || c.sym || c.strId >= 0) return r;
                bool t = c.fp ? c.d != 0 : c.i != 0;
                return constEval(n->kids[t ? 1 : 2]);
            }
            case NK::Comma:
                return r;
            default:
                return r;
        }
    }
};

}

void analyze(Program& prog) {
    prog.root = parseRoot;
    Sema s(prog);
    s.run(parseRoot);
}
