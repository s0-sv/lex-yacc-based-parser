#include <sys/mman.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "ast.hpp"

namespace {

const long MEM_SIZE = 1L << 30;
const long GUARD = 4096;
const long STACK_SIZE = 64L << 20;
const long FUNC_BASE = 1L << 40;
const int MAX_DEPTH = 20000;
const size_t CAPTURE_LIMIT = 1 << 20;

struct V {
    long i = 0;
    double d = 0;
};

struct ExitSignal {
    int code;
};

struct RuntimeError {
    std::string msg;
};

enum class Flow { Normal, Break, Continue, Return, Goto };

class Interp {
public:
    Interp(Program& p, long maxSteps, bool capture) : P(p), limit(maxSteps), capturing(capture) {
        void* m = mmap(nullptr, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (m == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
        mem = (uint8_t*)m;
    }

    RunResult run() {
        RunResult r;
        try {
            layout();
            Symbol* mainSym = nullptr;
            for (FuncInfo* f : P.functions) {
                if (f->sym->name == "main") mainSym = f->sym;
            }
            if (!mainSym) throw RuntimeError{"у програмі немає функції main"};
            std::vector<V> args;
            size_t np = mainSym->fn->params.size();
            if (np >= 1) {
                V argc;
                argc.i = 1;
                args.push_back(argc);
            }
            if (np >= 2) {
                long name = alloc(8);
                memcpy(mem + name, "a.out", 6);
                long argv = alloc(16);
                poke64(argv, name);
                poke64(argv + 8, 0);
                V a;
                a.i = argv;
                args.push_back(a);
            }
            while (args.size() < np) args.push_back(V());
            V ret = callUser(mainSym, args);
            r.exitCode = (int)(ret.i & 255);
        } catch (const ExitSignal& e) {
            r.exitCode = e.code & 255;
        } catch (const RuntimeError& e) {
            r.failed = true;
            r.error = e.msg;
            r.errorLine = curLine;
        }
        r.captured = captured;
        r.steps = steps;
        if (!capturing) fflush(stdout);
        return r;
    }

private:
    Program& P;
    long limit;
    bool capturing;
    uint8_t* mem = nullptr;
    long dataEnd = GUARD;
    long stackBase = 0, stackEnd = 0, sp = 0, fp = 0;
    long heapTop = 0;
    long steps = 0;
    int depth = 0;
    int curLine = 0;
    std::string captured;
    std::vector<long> stringAddr;
    std::map<long, std::vector<long>> freeLists;
    V retv;
    Node* seek = nullptr;
    Node* gotoTarget = nullptr;

    long reserve(long size, long align) {
        long a = (dataEnd + align - 1) / align * align;
        dataEnd = a + size;
        return a;
    }

    void layout() {
        for (const auto& s : P.strings) {
            long a = reserve((long)s.size() + 1, 1);
            memcpy(mem + a, s.data(), s.size());
            stringAddr.push_back(a);
        }
        for (GlobalObj* g : P.globals) {
            long a = alignOf(g->sym->type);
            g->sym->addr = reserve((long)g->bytes.size(), a < 1 ? 1 : a);
            if (!g->bytes.empty()) memcpy(mem + g->sym->addr, g->bytes.data(), g->bytes.size());
        }
        for (GlobalObj* g : P.globals) {
            for (const Reloc& r : g->relocs) {
                long target = r.strId >= 0 ? stringAddr[r.strId] : symAddr(r.sym);
                poke64(g->sym->addr + r.offset, target + r.addend);
            }
        }
        stackBase = (dataEnd + 4095) / 4096 * 4096;
        stackEnd = stackBase + STACK_SIZE;
        sp = stackBase;
        fp = stackBase;
        heapTop = stackEnd;
    }

    long symAddr(Symbol* s) {
        if (s->kind == SymKind::Func) return FUNC_BASE + s->id;
        if (s->global) {
            if (!s->hasStorage) throw RuntimeError{"змінну '" + s->name + "' не визначено"};
            return s->addr;
        }
        return fp + s->offset;
    }

    void check(long addr, long n) {
        if (addr < GUARD || n < 0 || n > heapTop || addr > heapTop - n) {
            if (addr >= 0 && addr < GUARD) throw RuntimeError{"звернення за нульовим вказівником"};
            throw RuntimeError{"звернення поза межами пам'яті (адреса " + std::to_string(addr) + ")"};
        }
    }

    void poke64(long a, long v) {
        memcpy(mem + a, &v, 8);
    }

    V load(Type* t, long a) {
        V v;
        switch (t->k) {
            case TK::Array:
            case TK::Struct:
            case TK::Func:
                v.i = a;
                return v;
            case TK::Float: {
                check(a, 4);
                float f;
                memcpy(&f, mem + a, 4);
                v.d = f;
                return v;
            }
            case TK::Double:
                check(a, 8);
                memcpy(&v.d, mem + a, 8);
                return v;
            case TK::Void:
                return v;
            default: {
                long n = sizeOf(t);
                check(a, n);
                long raw = 0;
                memcpy(&raw, mem + a, n);
                v.i = normInt(t, raw);
                return v;
            }
        }
    }

    void store(Type* t, long a, const V& v) {
        switch (t->k) {
            case TK::Struct: {
                long n = sizeOf(t);
                check(a, n);
                check(v.i, n);
                memmove(mem + a, mem + v.i, n);
                return;
            }
            case TK::Float: {
                check(a, 4);
                float f = (float)v.d;
                memcpy(mem + a, &f, 4);
                return;
            }
            case TK::Double:
                check(a, 8);
                memcpy(mem + a, &v.d, 8);
                return;
            default: {
                long n = sizeOf(t);
                check(a, n);
                memcpy(mem + a, &v.i, n);
            }
        }
    }

    void tick() {
        if (++steps > limit) throw RuntimeError{"перевищено ліміт кроків виконання (" + std::to_string(limit) + ")"};
    }

    bool truthy(Type* t, const V& v) {
        return isFp(t) ? v.d != 0 : v.i != 0;
    }

    static bool contains(Node* n, Node* t) {
        return n->lo <= t->lo && t->hi <= n->hi;
    }

    long addrOf(Node* n) {
        switch (n->kind) {
            case NK::Ident:
                return symAddr(n->sym);
            case NK::StrLit:
                return stringAddr[n->ival];
            case NK::Deref:
                return eval(n->kids[0]).i;
            case NK::Index: {
                long base = eval(n->kids[0]).i;
                long idx = eval(n->kids[1]).i;
                return base + idx * n->scale;
            }
            case NK::Member: {
                long base = n->scale ? eval(n->kids[0]).i : addrOf(n->kids[0]);
                return base + n->ival;
            }
            default:
                return eval(n).i;
        }
    }

    V applyBinary(Op op, Type* t, const V& a, const V& b, long scale, Type* resultType) {
        V r;
        if (isPtr(t)) {
            r.i = op == Op::Add ? a.i + b.i * scale : a.i - b.i * scale;
            return r;
        }
        if (isFp(t)) {
            r.d = fpBinary(op, t->k == TK::Float, a.d, b.d);
            return r;
        }
        bool divZero = false;
        r.i = intBinary(op, resultType, a.i, b.i, &divZero);
        if (divZero) throw RuntimeError{"ділення на нуль"};
        return r;
    }

    V castValue(Type* from, Type* to, const V& v) {
        V r;
        if (to->k == TK::Void) return r;
        if (to->k == TK::Struct) return v;
        if (isFp(to)) r.d = castToFp(from, to, v.i, v.d);
        else r.i = castToInt(from, to, v.i, v.d);
        return r;
    }

    V eval(Node* n) {
        V r;
        switch (n->kind) {
            case NK::IntLit:
            case NK::CharLit:
            case NK::SizeofExpr:
            case NK::SizeofType:
                r.i = n->ival;
                return r;
            case NK::FloatLit:
                r.d = n->dval;
                return r;
            case NK::StrLit:
                r.i = stringAddr[n->ival];
                return r;
            case NK::Ident: {
                Symbol* s = n->sym;
                if (s->kind == SymKind::EnumConst) {
                    r.i = s->value;
                    return r;
                }
                if (s->kind == SymKind::Func) {
                    r.i = FUNC_BASE + s->id;
                    return r;
                }
                return load(n->type, symAddr(s));
            }
            case NK::Deref:
            case NK::Index:
            case NK::Member:
                return load(n->type, addrOf(n));
            case NK::AddrOf:
                r.i = addrOf(n->kids[0]);
                return r;
            case NK::Cast:
            case NK::ImplicitCast: {
                Node* inner = n->kind == NK::Cast ? n->kids[1] : n->kids[0];
                if (inner->type->k == TK::Array) {
                    r.i = addrOf(inner);
                    return r;
                }
                return castValue(inner->type, n->type, eval(inner));
            }
            case NK::Binary:
                return binary(n);
            case NK::Unary: {
                V a = eval(n->kids[0]);
                Type* t = n->kids[0]->type;
                switch (n->op) {
                    case Op::Pos: return a;
                    case Op::Neg:
                        if (isFp(t)) r.d = -a.d;
                        else r.i = normInt(n->type, (long)(0UL - (unsigned long)a.i));
                        return r;
                    case Op::BitNot:
                        r.i = normInt(n->type, ~a.i);
                        return r;
                    case Op::Not:
                        r.i = !truthy(t, a);
                        return r;
                    default:
                        return r;
                }
            }
            case NK::Assign: {
                long a = addrOf(n->kids[0]);
                V v = eval(n->kids[1]);
                store(n->type, a, v);
                if (n->type->k == TK::Struct) v.i = a;
                return v;
            }
            case NK::CompoundAssign: {
                Node* lhs = n->kids[0];
                long a = addrOf(lhs);
                V cur = castValue(lhs->type, n->opType, load(lhs->type, a));
                V rhs = eval(n->kids[1]);
                V res = applyBinary(n->op, n->opType, cur, rhs, n->scale, n->opType);
                V out = castValue(n->opType, n->type, res);
                store(n->type, a, out);
                return out;
            }
            case NK::PreInc:
            case NK::PreDec:
            case NK::PostInc:
            case NK::PostDec: {
                Node* lhs = n->kids[0];
                long a = addrOf(lhs);
                V old = load(lhs->type, a);
                V now = old;
                int dir = (n->kind == NK::PreInc || n->kind == NK::PostInc) ? 1 : -1;
                if (isFp(lhs->type)) {
                    now.d = old.d + dir;
                    if (lhs->type->k == TK::Float) now.d = (float)now.d;
                } else if (lhs->type->k == TK::Bool) {
                    now.i = normInt(lhs->type, old.i + dir);
                } else {
                    now.i = normInt(lhs->type, old.i + dir * n->scale);
                }
                store(lhs->type, a, now);
                return (n->kind == NK::PreInc || n->kind == NK::PreDec) ? now : old;
            }
            case NK::Ternary: {
                V c = eval(n->kids[0]);
                return eval(n->kids[truthy(n->kids[0]->type, c) ? 1 : 2]);
            }
            case NK::Comma:
                eval(n->kids[0]);
                return eval(n->kids[1]);
            case NK::Call:
                return call(n);
            default:
                throw RuntimeError{"невідомий вираз у виконанні"};
        }
    }

    V binary(Node* n) {
        V r;
        Node* ka = n->kids[0];
        Node* kb = n->kids[1];
        if (n->op == Op::LAnd) {
            V a = eval(ka);
            if (!truthy(ka->type, a)) return r;
            V b = eval(kb);
            r.i = truthy(kb->type, b);
            return r;
        }
        if (n->op == Op::LOr) {
            V a = eval(ka);
            if (truthy(ka->type, a)) {
                r.i = 1;
                return r;
            }
            V b = eval(kb);
            r.i = truthy(kb->type, b);
            return r;
        }
        V a = eval(ka);
        V b = eval(kb);
        if (isCompare(n->op)) {
            if (isFp(ka->type)) r.i = fpCompare(n->op, a.d, b.d);
            else r.i = intCompare(n->op, ka->type->uns, a.i, b.i);
            return r;
        }
        if (isPtr(ka->type) && isPtr(kb->type)) {
            r.i = (a.i - b.i) / n->scale;
            return r;
        }
        return applyBinary(n->op, isPtr(ka->type) ? ka->type : n->type, a, b, n->scale, n->type);
    }

    V call(Node* n) {
        V target = eval(n->kids[0]);
        long id = target.i - FUNC_BASE;
        if (id < 0 || id >= (long)P.funcSyms.size()) {
            if (target.i == 0) throw RuntimeError{"виклик за нульовим вказівником на функцію"};
            throw RuntimeError{"виклик за некоректним вказівником на функцію"};
        }
        Symbol* f = P.funcSyms[id];
        std::vector<V> args;
        args.reserve(n->kids.size());
        for (size_t i = 1; i < n->kids.size(); i++) args.push_back(eval(n->kids[i]));
        int line = n->line;
        if (!f->fn) {
            curLine = line;
            return builtin(f->name, args);
        }
        V ret = callUser(f, args);
        curLine = line;
        if (n->type->k == TK::Struct) {
            long tmp = fp + n->tempOffset;
            long size = sizeOf(n->type);
            memmove(mem + tmp, mem + ret.i, size);
            ret.i = tmp;
        }
        return ret;
    }

    V callUser(Symbol* f, std::vector<V>& args) {
        FuncInfo* fi = f->fn;
        if (++depth > MAX_DEPTH) throw RuntimeError{"перевищено глибину рекурсії"};
        long savedFp = fp, savedSp = sp;
        long newFp = (sp + 15) / 16 * 16;
        long newSp = newFp + fi->frameSize;
        if (newSp > stackEnd) throw RuntimeError{"переповнення стека"};
        for (size_t i = 0; i < fi->params.size() && i < args.size(); i++) {
            Symbol* p = fi->params[i];
            store(p->type, newFp + p->offset, args[i]);
        }
        fp = newFp;
        sp = newSp;
        retv = V();
        Flow fl = exec(fi->body);
        if (fl == Flow::Goto) throw RuntimeError{"goto без цілі"};
        V ret = fl == Flow::Return ? retv : V();
        if (fl != Flow::Return && f->name == "main") ret.i = 0;
        fp = savedFp;
        sp = savedSp;
        depth--;
        return ret;
    }

    void runInits(Node* decl) {
        for (size_t i = 1; i < decl->kids.size(); i++) {
            Node* v = decl->kids[i];
            if (!v->sym || v->sym->kind != SymKind::Var || v->sym->global || v->inits.empty()) continue;
            long base = fp + v->sym->offset;
            long size = sizeOf(v->sym->type);
            if (isAgg(v->sym->type)) memset(mem + base, 0, size);
            for (const InitOp& op : v->inits) {
                if (!op.expr) {
                    mem[base + op.offset] = (uint8_t)op.byte;
                } else {
                    V value = eval(op.expr);
                    store(op.type, base + op.offset, value);
                }
            }
        }
    }

    Flow exec(Node* n) {
        if (seek && !contains(n, seek)) return Flow::Normal;
        tick();
        if (!seek) curLine = n->line;
        switch (n->kind) {
            case NK::Block: {
                size_t i = 0;
                while (i < n->kids.size()) {
                    Node* k = n->kids[i];
                    Flow f;
                    if (k->kind == NK::Declaration) {
                        if (!seek) {
                            curLine = k->line;
                            runInits(k);
                        }
                        f = Flow::Normal;
                    } else {
                        f = exec(k);
                    }
                    if (f == Flow::Goto && contains(n, gotoTarget)) {
                        seek = gotoTarget;
                        i = 0;
                        continue;
                    }
                    if (f != Flow::Normal) return f;
                    i++;
                }
                return Flow::Normal;
            }
            case NK::ExprStmt:
                eval(n->kids[0]);
                return Flow::Normal;
            case NK::If: {
                if (seek) {
                    if (contains(n->kids[1], seek)) return exec(n->kids[1]);
                    return exec(n->kids[2]);
                }
                V c = eval(n->kids[0]);
                if (truthy(n->kids[0]->type, c)) return exec(n->kids[1]);
                if (n->kids[2]) return exec(n->kids[2]);
                return Flow::Normal;
            }
            case NK::While:
                for (;;) {
                    if (!seek) {
                        tick();
                        V c = eval(n->kids[0]);
                        if (!truthy(n->kids[0]->type, c)) return Flow::Normal;
                    }
                    Flow f = exec(n->kids[1]);
                    if (f == Flow::Break) return Flow::Normal;
                    if (f == Flow::Return || f == Flow::Goto) return f;
                }
            case NK::DoWhile:
                for (;;) {
                    Flow f = exec(n->kids[0]);
                    if (f == Flow::Break) return Flow::Normal;
                    if (f == Flow::Return || f == Flow::Goto) return f;
                    tick();
                    V c = eval(n->kids[1]);
                    if (!truthy(n->kids[1]->type, c)) return Flow::Normal;
                }
            case NK::For: {
                if (!seek && n->kids[0]) {
                    if (n->kids[0]->kind == NK::Declaration) runInits(n->kids[0]);
                    else eval(n->kids[0]);
                }
                for (;;) {
                    if (!seek && n->kids[1]) {
                        tick();
                        V c = eval(n->kids[1]);
                        if (!truthy(n->kids[1]->type, c)) return Flow::Normal;
                    }
                    Flow f = exec(n->kids[3]);
                    if (f == Flow::Break) return Flow::Normal;
                    if (f == Flow::Return || f == Flow::Goto) return f;
                    if (n->kids[2]) eval(n->kids[2]);
                }
            }
            case NK::Switch: {
                if (!seek) {
                    long v = eval(n->kids[0]).i;
                    Node* chosen = n->target;
                    for (Node* c : n->cases) {
                        if (c->ival == v) {
                            chosen = c;
                            break;
                        }
                    }
                    if (!chosen) return Flow::Normal;
                    seek = chosen;
                }
                Flow f = exec(n->kids[1]);
                if (f == Flow::Break) return Flow::Normal;
                return f;
            }
            case NK::Case:
                if (seek == n) seek = nullptr;
                return exec(n->kids[1]);
            case NK::Default:
            case NK::Label:
                if (seek == n) seek = nullptr;
                return exec(n->kids[0]);
            case NK::Break:
                return Flow::Break;
            case NK::Continue:
                return Flow::Continue;
            case NK::Return:
                if (n->kids[0]) retv = eval(n->kids[0]);
                else retv = V();
                return Flow::Return;
            case NK::Goto:
                gotoTarget = n->target;
                return Flow::Goto;
            default:
                return Flow::Normal;
        }
    }

    void out(const char* s, size_t n) {
        if (capturing) {
            if (captured.size() + n > CAPTURE_LIMIT) throw RuntimeError{"перевищено ліміт виводу програми"};
            captured.append(s, n);
        } else {
            fwrite(s, 1, n, stdout);
        }
    }

    std::string cstr(long a) {
        std::string s;
        for (;;) {
            check(a, 1);
            char c = (char)mem[a++];
            if (!c) return s;
            s.push_back(c);
        }
    }

    long alloc(long n) {
        if (n < 0 || n > MEM_SIZE) return 0;
        long size = (n + 15) / 16 * 16;
        if (size == 0) size = 16;
        auto f = freeLists.find(size);
        if (f != freeLists.end() && !f->second.empty()) {
            long a = f->second.back();
            f->second.pop_back();
            return a;
        }
        long hdr = heapTop;
        long a = hdr + 16;
        if (a + size > MEM_SIZE) return 0;
        heapTop = a + size;
        poke64(hdr, size);
        return a;
    }

    long blockSize(long a) {
        check(a - 16, 8);
        long size;
        memcpy(&size, mem + a - 16, 8);
        return size;
    }

    std::string format(const std::vector<V>& args, size_t argIdx) {
        std::string fmt = cstr(args[argIdx++].i);
        std::string o;
        auto next = [&]() -> V {
            if (argIdx >= args.size()) throw RuntimeError{"форматному рядку бракує аргументів"};
            return args[argIdx++];
        };
        for (size_t i = 0; i < fmt.size(); i++) {
            if (fmt[i] != '%') {
                o.push_back(fmt[i]);
                continue;
            }
            std::string spec = "%";
            i++;
            while (i < fmt.size() && strchr("-+ #0", fmt[i])) spec.push_back(fmt[i++]);
            while (i < fmt.size() && (isdigit((unsigned char)fmt[i]) || fmt[i] == '*')) {
                if (fmt[i] == '*') spec += std::to_string((int)next().i);
                else spec.push_back(fmt[i]);
                i++;
            }
            if (i < fmt.size() && fmt[i] == '.') {
                spec.push_back(fmt[i++]);
                while (i < fmt.size() && (isdigit((unsigned char)fmt[i]) || fmt[i] == '*')) {
                    if (fmt[i] == '*') spec += std::to_string((int)next().i);
                    else spec.push_back(fmt[i]);
                    i++;
                }
            }
            std::string length;
            while (i < fmt.size() && strchr("hlLzjt", fmt[i])) length.push_back(fmt[i++]);
            if (i >= fmt.size()) throw RuntimeError{"незавершений специфікатор формату"};
            char c = fmt[i];
            std::string buf;
            auto emit = [&](auto value, const std::string& full) {
                int need = snprintf(nullptr, 0, full.c_str(), value);
                if (need < 0 || (size_t)need > CAPTURE_LIMIT) throw RuntimeError{"завеликий результат форматування"};
                buf.resize(need + 1);
                snprintf(&buf[0], need + 1, full.c_str(), value);
                buf.resize(need);
                o += buf;
            };
            switch (c) {
                case '%':
                    o.push_back('%');
                    break;
                case 'd':
                case 'i': {
                    long v = next().i;
                    if (length == "hh") v = (signed char)v;
                    else if (length == "h") v = (short)v;
                    else if (length.empty()) v = (int)v;
                    emit(v, spec + "l" + c);
                    break;
                }
                case 'u':
                case 'x':
                case 'X':
                case 'o': {
                    unsigned long v = (unsigned long)next().i;
                    if (length == "hh") v = (unsigned char)v;
                    else if (length == "h") v = (unsigned short)v;
                    else if (length.empty()) v = (unsigned int)v;
                    emit(v, spec + "l" + c);
                    break;
                }
                case 'c':
                    emit((int)next().i, spec + "c");
                    break;
                case 's': {
                    long a = next().i;
                    if (a == 0) {
                        emit("(null)", spec + "s");
                    } else {
                        std::string s = cstr(a);
                        emit(s.c_str(), spec + "s");
                    }
                    break;
                }
                case 'p':
                    emit((void*)next().i, spec + "p");
                    break;
                case 'f':
                case 'F':
                case 'e':
                case 'E':
                case 'g':
                case 'G':
                    emit(next().d, spec + c);
                    break;
                default:
                    throw RuntimeError{std::string("непідтримуваний специфікатор формату %") + c};
            }
        }
        return o;
    }

    int scan(const std::vector<V>& args) {
        std::string fmt = cstr(args[0].i);
        size_t argIdx = 1;
        int count = 0;
        auto skipWs = []() {
            int c;
            while ((c = getchar()) != EOF && isspace(c)) {
            }
            if (c != EOF) ungetc(c, stdin);
        };
        for (size_t i = 0; i < fmt.size(); i++) {
            if (isspace((unsigned char)fmt[i])) {
                skipWs();
                continue;
            }
            if (fmt[i] != '%') {
                int c = getchar();
                if (c != (unsigned char)fmt[i]) {
                    if (c != EOF) ungetc(c, stdin);
                    return count;
                }
                continue;
            }
            i++;
            std::string length;
            while (i < fmt.size() && strchr("hlLzjt", fmt[i])) length.push_back(fmt[i++]);
            if (i >= fmt.size()) break;
            char c = fmt[i];
            if (c == '%') continue;
            if (argIdx >= args.size()) throw RuntimeError{"scanf бракує аргументів"};
            long target = args[argIdx++].i;
            if (c == 'c') {
                int ch = getchar();
                if (ch == EOF) return count ? count : -1;
                check(target, 1);
                mem[target] = (uint8_t)ch;
                count++;
                continue;
            }
            skipWs();
            std::string tok;
            int ch;
            while ((ch = getchar()) != EOF && !isspace(ch)) {
                bool accept = c == 's' || isalnum(ch) || ch == '-' || ch == '+' || ch == '.';
                if (!accept) break;
                tok.push_back((char)ch);
            }
            if (ch != EOF) ungetc(ch, stdin);
            if (tok.empty()) return (ch == EOF && !count) ? -1 : count;
            if (c == 's') {
                check(target, (long)tok.size() + 1);
                memcpy(mem + target, tok.c_str(), tok.size() + 1);
            } else if (c == 'd' || c == 'i' || c == 'u' || c == 'x') {
                long v = strtol(tok.c_str(), nullptr, c == 'x' ? 16 : c == 'i' ? 0 : 10);
                long n = length == "hh" ? 1 : length == "h" ? 2 : length.empty() ? 4 : 8;
                check(target, n);
                memcpy(mem + target, &v, n);
            } else if (c == 'f' || c == 'g' || c == 'e') {
                double v = strtod(tok.c_str(), nullptr);
                if (length == "l") {
                    check(target, 8);
                    memcpy(mem + target, &v, 8);
                } else {
                    float f = (float)v;
                    check(target, 4);
                    memcpy(mem + target, &f, 4);
                }
            } else {
                throw RuntimeError{std::string("непідтримуваний специфікатор scanf %") + c};
            }
            count++;
        }
        return count;
    }

    V builtin(const std::string& name, const std::vector<V>& a) {
        V r;
        auto math1 = [&](double (*f)(double)) {
            r.d = f(a[0].d);
            return r;
        };
        if (name == "printf") {
            std::string s = format(a, 0);
            out(s.data(), s.size());
            r.i = (long)s.size();
            return r;
        }
        if (name == "sprintf" || name == "snprintf") {
            bool bounded = name == "snprintf";
            std::string s = format(a, bounded ? 2 : 1);
            long dest = a[0].i;
            long cap = bounded ? a[1].i : (long)s.size() + 1;
            if (cap > 0) {
                long n = std::min((long)s.size(), cap - 1);
                check(dest, n + 1);
                memcpy(mem + dest, s.data(), n);
                mem[dest + n] = 0;
            }
            r.i = (long)s.size();
            return r;
        }
        if (name == "scanf") {
            r.i = scan(a);
            return r;
        }
        if (name == "puts") {
            std::string s = cstr(a[0].i);
            s.push_back('\n');
            out(s.data(), s.size());
            r.i = (long)s.size();
            return r;
        }
        if (name == "putchar") {
            char c = (char)a[0].i;
            out(&c, 1);
            r.i = (unsigned char)c;
            return r;
        }
        if (name == "getchar") {
            r.i = getchar();
            return r;
        }
        if (name == "malloc") {
            r.i = alloc(a[0].i);
            return r;
        }
        if (name == "calloc") {
            if (a[0].i < 0 || a[1].i < 0 || (a[1].i && a[0].i > MEM_SIZE / a[1].i)) return r;
            long n = a[0].i * a[1].i;
            r.i = alloc(n);
            if (r.i) {
                check(r.i, n);
                memset(mem + r.i, 0, n);
            }
            return r;
        }
        if (name == "realloc") {
            if (a[0].i == 0) {
                r.i = alloc(a[1].i);
                return r;
            }
            long old = blockSize(a[0].i);
            long fresh = alloc(a[1].i);
            if (fresh) {
                long keep = std::min(old, a[1].i);
                check(a[0].i, keep);
                check(fresh, keep);
                memmove(mem + fresh, mem + a[0].i, keep);
                freeLists[old].push_back(a[0].i);
            }
            r.i = fresh;
            return r;
        }
        if (name == "free") {
            if (a[0].i) freeLists[blockSize(a[0].i)].push_back(a[0].i);
            return r;
        }
        if (name == "strlen") {
            r.i = (long)cstr(a[0].i).size();
            return r;
        }
        if (name == "strcmp") {
            int c = strcmp(cstr(a[0].i).c_str(), cstr(a[1].i).c_str());
            r.i = c < 0 ? -1 : c > 0 ? 1 : 0;
            return r;
        }
        if (name == "strncmp") {
            int c = strncmp(cstr(a[0].i).c_str(), cstr(a[1].i).c_str(), (size_t)a[2].i);
            r.i = c < 0 ? -1 : c > 0 ? 1 : 0;
            return r;
        }
        if (name == "strcpy") {
            std::string s = cstr(a[1].i);
            check(a[0].i, (long)s.size() + 1);
            memcpy(mem + a[0].i, s.c_str(), s.size() + 1);
            r.i = a[0].i;
            return r;
        }
        if (name == "strncpy") {
            std::string s = cstr(a[1].i);
            long n = a[2].i;
            check(a[0].i, n);
            for (long i = 0; i < n; i++) mem[a[0].i + i] = i < (long)s.size() ? (uint8_t)s[i] : 0;
            r.i = a[0].i;
            return r;
        }
        if (name == "strcat") {
            std::string d = cstr(a[0].i);
            std::string s = cstr(a[1].i);
            check(a[0].i, (long)(d.size() + s.size() + 1));
            memcpy(mem + a[0].i + d.size(), s.c_str(), s.size() + 1);
            r.i = a[0].i;
            return r;
        }
        if (name == "strchr") {
            std::string s = cstr(a[0].i);
            char c = (char)a[1].i;
            if (c == 0) {
                r.i = a[0].i + (long)s.size();
                return r;
            }
            size_t p = s.find(c);
            r.i = p == std::string::npos ? 0 : a[0].i + (long)p;
            return r;
        }
        if (name == "memset") {
            check(a[0].i, a[2].i);
            memset(mem + a[0].i, (int)a[1].i, (size_t)a[2].i);
            r.i = a[0].i;
            return r;
        }
        if (name == "memcpy" || name == "memmove") {
            check(a[0].i, a[2].i);
            check(a[1].i, a[2].i);
            memmove(mem + a[0].i, mem + a[1].i, (size_t)a[2].i);
            r.i = a[0].i;
            return r;
        }
        if (name == "memcmp") {
            check(a[0].i, a[2].i);
            check(a[1].i, a[2].i);
            int c = memcmp(mem + a[0].i, mem + a[1].i, (size_t)a[2].i);
            r.i = c < 0 ? -1 : c > 0 ? 1 : 0;
            return r;
        }
        if (name == "abs") {
            r.i = normInt(tyInt, a[0].i < 0 ? -a[0].i : a[0].i);
            return r;
        }
        if (name == "labs") {
            r.i = a[0].i < 0 ? -a[0].i : a[0].i;
            return r;
        }
        if (name == "atoi") {
            r.i = atoi(cstr(a[0].i).c_str());
            return r;
        }
        if (name == "atol") {
            r.i = atol(cstr(a[0].i).c_str());
            return r;
        }
        if (name == "atof") {
            r.d = atof(cstr(a[0].i).c_str());
            return r;
        }
        if (name == "exit") throw ExitSignal{(int)a[0].i};
        if (name == "rand") {
            r.i = rand();
            return r;
        }
        if (name == "srand") {
            srand((unsigned)a[0].i);
            return r;
        }
        if (name == "sqrt") return math1(std::sqrt);
        if (name == "sin") return math1(std::sin);
        if (name == "cos") return math1(std::cos);
        if (name == "tan") return math1(std::tan);
        if (name == "atan") return math1(std::atan);
        if (name == "exp") return math1(std::exp);
        if (name == "log") return math1(std::log);
        if (name == "log10") return math1(std::log10);
        if (name == "floor") return math1(std::floor);
        if (name == "ceil") return math1(std::ceil);
        if (name == "fabs") return math1(std::fabs);
        if (name == "pow") {
            r.d = std::pow(a[0].d, a[1].d);
            return r;
        }
        if (name == "fmod") {
            r.d = std::fmod(a[0].d, a[1].d);
            return r;
        }
        if (name == "atan2") {
            r.d = std::atan2(a[0].d, a[1].d);
            return r;
        }
        throw RuntimeError{"функцію '" + name + "' не реалізовано"};
    }
};

}

RunResult interpret(Program& prog, long maxSteps, bool capture) {
    Interp in(prog, maxSteps, capture);
    return in.run();
}
