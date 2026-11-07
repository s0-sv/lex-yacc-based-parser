#include <cstring>
#include <map>
#include <set>
#include <sstream>

#include "ast.hpp"

namespace {

const char* GP64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};

class CodeGen {
public:
    explicit CodeGen(Program& p) : P(p) {}

    std::string run() {
        emitData();
        o << "\t.text\n";
        for (FuncInfo* f : P.functions) emitFunction(f);
        o << "\t.section .note.GNU-stack,\"\",@progbits\n";
        return o.str();
    }

private:
    Program& P;
    std::ostringstream o;
    int labelId = 0;
    int depth = 0;
    long total = 0;
    long localBase = 0;
    FuncInfo* cur = nullptr;
    std::string retLabel;
    std::vector<std::string> breaks, continues;
    std::map<Node*, std::string> caseLabels;

    std::string label() {
        return ".L" + std::to_string(++labelId);
    }

    void e(const std::string& s) {
        o << "\t" << s << "\n";
    }

    void place(const std::string& l) {
        o << l << ":\n";
    }

    void push() {
        e("pushq %rax");
        depth++;
    }

    void pop(const std::string& reg) {
        e("popq " + reg);
        depth--;
    }

    void pushf() {
        e("subq $8, %rsp");
        e("movsd %xmm0, (%rsp)");
        depth++;
    }

    void popf(int xmm) {
        e("movsd (%rsp), %xmm" + std::to_string(xmm));
        e("addq $8, %rsp");
        depth--;
    }

    void pushValue(Type* t) {
        if (isFp(t)) pushf();
        else push();
    }

    std::string local(long off) {
        return std::to_string(localBase + off) + "(%rbp)";
    }

    void emitData() {
        if (!P.strings.empty()) o << "\t.section .rodata\n";
        for (size_t i = 0; i < P.strings.size(); i++) {
            place(".LC" + std::to_string(i));
            emitBytes((const uint8_t*)P.strings[i].data(), P.strings[i].size());
            e(".byte 0");
        }
        if (!P.globals.empty()) o << "\t.data\n";
        for (GlobalObj* g : P.globals) {
            Symbol* s = g->sym;
            if (!s->internal) e(".globl " + s->label);
            long a = alignOf(s->type);
            e(".align " + std::to_string(a < 1 ? 1 : a));
            e(".type " + s->label + ", @object");
            e(".size " + s->label + ", " + std::to_string(g->bytes.size()));
            place(s->label);
            size_t pos = 0;
            std::map<long, const Reloc*> rel;
            for (const Reloc& r : g->relocs) rel[r.offset] = &r;
            for (const auto& entry : rel) {
                emitRun(g->bytes, pos, (size_t)entry.first);
                const Reloc* r = entry.second;
                std::string target = r->strId >= 0 ? ".LC" + std::to_string(r->strId) : r->sym->label;
                if (r->addend) target += (r->addend > 0 ? "+" : "") + std::to_string(r->addend);
                e(".quad " + target);
                pos = (size_t)entry.first + 8;
            }
            emitRun(g->bytes, pos, g->bytes.size());
            if (g->bytes.empty()) e(".zero 1");
        }
    }

    void emitRun(const std::vector<uint8_t>& bytes, size_t from, size_t to) {
        size_t i = from;
        while (i < to) {
            if (bytes[i] == 0) {
                size_t j = i;
                while (j < to && bytes[j] == 0) j++;
                e(".zero " + std::to_string(j - i));
                i = j;
            } else {
                size_t j = i;
                while (j < to && bytes[j] != 0) j++;
                emitBytes(bytes.data() + i, j - i);
                i = j;
            }
        }
    }

    void emitBytes(const uint8_t* b, size_t n) {
        for (size_t i = 0; i < n; i += 16) {
            std::string line = ".byte ";
            for (size_t j = i; j < n && j < i + 16; j++) {
                if (j > i) line += ",";
                line += std::to_string(b[j]);
            }
            e(line);
        }
    }

    void loadFromRax(Type* t) {
        switch (t->k) {
            case TK::Array:
            case TK::Struct:
            case TK::Func:
            case TK::Void:
                return;
            case TK::Float:
                e("movss (%rax), %xmm0");
                return;
            case TK::Double:
                e("movsd (%rax), %xmm0");
                return;
            case TK::Bool:
                e("movzbq (%rax), %rax");
                return;
            case TK::Char:
                e(t->uns ? "movzbq (%rax), %rax" : "movsbq (%rax), %rax");
                return;
            case TK::Short:
                e(t->uns ? "movzwq (%rax), %rax" : "movswq (%rax), %rax");
                return;
            case TK::Int:
                e(t->uns ? "movl (%rax), %eax" : "movslq (%rax), %rax");
                return;
            default:
                e("movq (%rax), %rax");
        }
    }

    void storeToRdi(Type* t) {
        switch (t->k) {
            case TK::Struct: {
                e("movq %rdi, %rdx");
                e("movq %rax, %rsi");
                e("movq $" + std::to_string(sizeOf(t)) + ", %rcx");
                e("rep movsb");
                e("movq %rdx, %rax");
                return;
            }
            case TK::Float:
                e("movss %xmm0, (%rdi)");
                return;
            case TK::Double:
                e("movsd %xmm0, (%rdi)");
                return;
            case TK::Bool:
            case TK::Char:
                e("movb %al, (%rdi)");
                return;
            case TK::Short:
                e("movw %ax, (%rdi)");
                return;
            case TK::Int:
                e("movl %eax, (%rdi)");
                return;
            default:
                e("movq %rax, (%rdi)");
        }
    }

    void normalize(Type* t) {
        switch (t->k) {
            case TK::Bool:
                e("cmpq $0, %rax");
                e("setne %al");
                e("movzbq %al, %rax");
                return;
            case TK::Char:
                e(t->uns ? "movzbq %al, %rax" : "movsbq %al, %rax");
                return;
            case TK::Short:
                e(t->uns ? "movzwq %ax, %rax" : "movswq %ax, %rax");
                return;
            case TK::Int:
                e(t->uns ? "movl %eax, %eax" : "movslq %eax, %rax");
                return;
            default:
                return;
        }
    }

    void toBool(Type* t) {
        if (isFp(t)) {
            bool single = t->k == TK::Float;
            e("xorps %xmm1, %xmm1");
            e(single ? "ucomiss %xmm1, %xmm0" : "ucomisd %xmm1, %xmm0");
            e("setne %al");
            e("setp %dl");
            e("orb %dl, %al");
            e("movzbq %al, %rax");
        } else {
            e("cmpq $0, %rax");
            e("setne %al");
            e("movzbq %al, %rax");
        }
    }

    void genCast(Type* from, Type* to) {
        if (to->k == TK::Void || to->k == TK::Struct) return;
        if (isFp(from)) {
            if (isFp(to)) {
                if (from->k == TK::Float && to->k == TK::Double) e("cvtss2sd %xmm0, %xmm0");
                else if (from->k == TK::Double && to->k == TK::Float) e("cvtsd2ss %xmm0, %xmm0");
                return;
            }
            if (to->k == TK::Bool) {
                toBool(from);
                return;
            }
            if (from->k == TK::Float) e("cvtss2sd %xmm0, %xmm0");
            if (to->k == TK::Long && to->uns) {
                std::string big = label(), end = label();
                e("movabsq $4890909195324358656, %rax");
                e("movq %rax, %xmm1");
                e("ucomisd %xmm1, %xmm0");
                e("jae " + big);
                e("cvttsd2siq %xmm0, %rax");
                e("jmp " + end);
                place(big);
                e("subsd %xmm1, %xmm0");
                e("cvttsd2siq %xmm0, %rax");
                e("btcq $63, %rax");
                place(end);
                return;
            }
            e("cvttsd2siq %xmm0, %rax");
            normalize(to);
            return;
        }
        if (isFp(to)) {
            const char* suffix = to->k == TK::Float ? "ss" : "sd";
            std::string conv = std::string("cvtsi2") + suffix + "q";
            if (from->k == TK::Long && from->uns) {
                std::string neg = label(), end = label();
                e("testq %rax, %rax");
                e("js " + neg);
                e(conv + " %rax, %xmm0");
                e("jmp " + end);
                place(neg);
                e("movq %rax, %rdi");
                e("andl $1, %eax");
                e("shrq %rdi");
                e("orq %rax, %rdi");
                e(conv + " %rdi, %xmm0");
                e(std::string("add") + suffix + " %xmm0, %xmm0");
                place(end);
                return;
            }
            e(conv + " %rax, %xmm0");
            return;
        }
        normalize(to);
    }

    void genAddr(Node* n) {
        switch (n->kind) {
            case NK::Ident: {
                Symbol* s = n->sym;
                if (s->kind == SymKind::Func) {
                    if (s->fn) e("leaq " + s->label + "(%rip), %rax");
                    else e("movq " + s->label + "@GOTPCREL(%rip), %rax");
                } else if (s->global) {
                    if (s->hasStorage) e("leaq " + s->label + "(%rip), %rax");
                    else e("movq " + s->label + "@GOTPCREL(%rip), %rax");
                } else {
                    e("leaq " + local(s->offset) + ", %rax");
                }
                return;
            }
            case NK::StrLit:
                e("leaq .LC" + std::to_string(n->ival) + "(%rip), %rax");
                return;
            case NK::Deref:
                gen(n->kids[0]);
                return;
            case NK::Index:
                gen(n->kids[1]);
                push();
                gen(n->kids[0]);
                pop("%rdi");
                e("imulq $" + std::to_string(n->scale) + ", %rdi, %rdi");
                e("addq %rdi, %rax");
                return;
            case NK::Member:
                if (n->scale) gen(n->kids[0]);
                else genAddr(n->kids[0]);
                if (n->ival) e("addq $" + std::to_string(n->ival) + ", %rax");
                return;
            default:
                gen(n);
        }
    }

    void loadImm(long v) {
        e("movabsq $" + std::to_string(v) + ", %rax");
    }

    void arith(Op op, Type* t, long scale) {
        if (isFp(t)) {
            const char* s = t->k == TK::Float ? "ss" : "sd";
            switch (op) {
                case Op::Add: e(std::string("add") + s + " %xmm1, %xmm0"); break;
                case Op::Sub: e(std::string("sub") + s + " %xmm1, %xmm0"); break;
                case Op::Mul: e(std::string("mul") + s + " %xmm1, %xmm0"); break;
                case Op::Div: e(std::string("div") + s + " %xmm1, %xmm0"); break;
                default: break;
            }
            return;
        }
        if (isPtr(t)) {
            e("imulq $" + std::to_string(scale) + ", %rdi, %rdi");
            e(op == Op::Add ? "addq %rdi, %rax" : "subq %rdi, %rax");
            return;
        }
        switch (op) {
            case Op::Add: e("addq %rdi, %rax"); break;
            case Op::Sub: e("subq %rdi, %rax"); break;
            case Op::Mul: e("imulq %rdi, %rax"); break;
            case Op::Div:
            case Op::Mod:
                if (t->uns) {
                    e("xorl %edx, %edx");
                    e("divq %rdi");
                } else {
                    e("cqto");
                    e("idivq %rdi");
                }
                if (op == Op::Mod) e("movq %rdx, %rax");
                break;
            case Op::Shl:
                e("movq %rdi, %rcx");
                e("shlq %cl, %rax");
                break;
            case Op::Shr:
                e("movq %rdi, %rcx");
                e(t->uns ? "shrq %cl, %rax" : "sarq %cl, %rax");
                break;
            case Op::BitAnd: e("andq %rdi, %rax"); break;
            case Op::BitOr: e("orq %rdi, %rax"); break;
            case Op::BitXor: e("xorq %rdi, %rax"); break;
            default: break;
        }
        normalize(t);
    }

    void compare(Op op, Type* t) {
        if (isFp(t)) {
            std::string cmp = t->k == TK::Float ? "ucomiss" : "ucomisd";
            switch (op) {
                case Op::Lt:
                    e(cmp + " %xmm0, %xmm1");
                    e("seta %al");
                    break;
                case Op::Le:
                    e(cmp + " %xmm0, %xmm1");
                    e("setae %al");
                    break;
                case Op::Gt:
                    e(cmp + " %xmm1, %xmm0");
                    e("seta %al");
                    break;
                case Op::Ge:
                    e(cmp + " %xmm1, %xmm0");
                    e("setae %al");
                    break;
                case Op::Eq:
                    e(cmp + " %xmm1, %xmm0");
                    e("sete %al");
                    e("setnp %dl");
                    e("andb %dl, %al");
                    break;
                default:
                    e(cmp + " %xmm1, %xmm0");
                    e("setne %al");
                    e("setp %dl");
                    e("orb %dl, %al");
            }
            e("movzbq %al, %rax");
            return;
        }
        e("cmpq %rdi, %rax");
        bool u = t->uns;
        switch (op) {
            case Op::Lt: e(u ? "setb %al" : "setl %al"); break;
            case Op::Le: e(u ? "setbe %al" : "setle %al"); break;
            case Op::Gt: e(u ? "seta %al" : "setg %al"); break;
            case Op::Ge: e(u ? "setae %al" : "setge %al"); break;
            case Op::Eq: e("sete %al"); break;
            default: e("setne %al");
        }
        e("movzbq %al, %rax");
    }

    void genCall(Node* n) {
        size_t nargs = n->kids.size() - 1;
        struct Slot {
            Node* arg;
            bool fp;
            int reg;
        };
        std::vector<Slot> regs;
        std::vector<Node*> stack;
        int gp = 0, fp = 0;
        bool hidden = n->type->k == TK::Struct;
        if (hidden) regs.push_back({nullptr, false, gp++});
        for (size_t i = 0; i < nargs; i++) {
            Node* a = n->kids[i + 1];
            if (isFp(a->type)) {
                if (fp < 8) regs.push_back({a, true, fp++});
                else stack.push_back(a);
            } else {
                if (gp < 6) regs.push_back({a, false, gp++});
                else stack.push_back(a);
            }
        }
        bool pad = (depth + (int)stack.size()) % 2 != 0;
        if (pad) {
            e("subq $8, %rsp");
            depth++;
        }
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            gen(*it);
            pushValue((*it)->type);
        }
        for (auto it = regs.rbegin(); it != regs.rend(); ++it) {
            if (!it->arg) {
                e("leaq " + local(n->tempOffset) + ", %rax");
                push();
                continue;
            }
            gen(it->arg);
            pushValue(it->arg->type);
        }
        Node* callee = n->kids[0];
        bool direct = callee->kind == NK::Ident && callee->sym && callee->sym->kind == SymKind::Func;
        if (!direct) {
            gen(callee);
            e("movq %rax, %r10");
        }
        for (const Slot& s : regs) {
            if (s.fp) popf(s.reg);
            else pop(GP64[s.reg]);
        }
        e("movl $" + std::to_string(fp) + ", %eax");
        if (direct) {
            Symbol* s = callee->sym;
            e("call " + s->label + (s->fn ? "" : "@PLT"));
        } else {
            e("call *%r10");
        }
        long cleanup = (long)stack.size() * 8 + (pad ? 8 : 0);
        if (cleanup) e("addq $" + std::to_string(cleanup) + ", %rsp");
        depth -= (int)stack.size() + (pad ? 1 : 0);
        if (isInt(n->type)) normalize(n->type);
    }

    void gen(Node* n) {
        switch (n->kind) {
            case NK::IntLit:
            case NK::CharLit:
            case NK::SizeofExpr:
            case NK::SizeofType:
                loadImm(n->ival);
                return;
            case NK::FloatLit:
                if (n->type->k == TK::Float) {
                    float f = (float)n->dval;
                    uint32_t bits;
                    memcpy(&bits, &f, 4);
                    e("movl $" + std::to_string(bits) + ", %eax");
                    e("movd %eax, %xmm0");
                } else {
                    long bits;
                    memcpy(&bits, &n->dval, 8);
                    loadImm(bits);
                    e("movq %rax, %xmm0");
                }
                return;
            case NK::StrLit:
                genAddr(n);
                return;
            case NK::Ident:
                if (n->sym->kind == SymKind::EnumConst) {
                    loadImm(n->sym->value);
                    return;
                }
                genAddr(n);
                loadFromRax(n->type);
                return;
            case NK::Deref:
            case NK::Index:
            case NK::Member:
                genAddr(n);
                loadFromRax(n->type);
                return;
            case NK::AddrOf:
                genAddr(n->kids[0]);
                return;
            case NK::Cast:
            case NK::ImplicitCast: {
                Node* inner = n->kind == NK::Cast ? n->kids[1] : n->kids[0];
                if (inner->type->k == TK::Array) {
                    genAddr(inner);
                    return;
                }
                gen(inner);
                genCast(inner->type, n->type);
                return;
            }
            case NK::Binary:
                genBinary(n);
                return;
            case NK::Unary:
                genUnary(n);
                return;
            case NK::Assign:
                genAddr(n->kids[0]);
                push();
                gen(n->kids[1]);
                pop("%rdi");
                storeToRdi(n->type);
                return;
            case NK::CompoundAssign:
                genCompound(n);
                return;
            case NK::PreInc:
            case NK::PreDec:
            case NK::PostInc:
            case NK::PostDec:
                genIncDec(n);
                return;
            case NK::Ternary: {
                std::string otherwise = label(), end = label();
                gen(n->kids[0]);
                toBool(n->kids[0]->type);
                e("testq %rax, %rax");
                e("je " + otherwise);
                gen(n->kids[1]);
                e("jmp " + end);
                place(otherwise);
                gen(n->kids[2]);
                place(end);
                return;
            }
            case NK::Comma:
                gen(n->kids[0]);
                gen(n->kids[1]);
                return;
            case NK::Call:
                genCall(n);
                return;
            default:
                return;
        }
    }

    void genBinary(Node* n) {
        Node* a = n->kids[0];
        Node* b = n->kids[1];
        if (n->op == Op::LAnd || n->op == Op::LOr) {
            std::string shortCircuit = label(), end = label();
            bool isAnd = n->op == Op::LAnd;
            gen(a);
            toBool(a->type);
            e("testq %rax, %rax");
            e((isAnd ? "je " : "jne ") + shortCircuit);
            gen(b);
            toBool(b->type);
            e("testq %rax, %rax");
            e((isAnd ? "je " : "jne ") + shortCircuit);
            loadImm(isAnd ? 1 : 0);
            e("jmp " + end);
            place(shortCircuit);
            loadImm(isAnd ? 0 : 1);
            place(end);
            return;
        }
        gen(b);
        pushValue(b->type);
        gen(a);
        if (isFp(b->type)) popf(1);
        else pop("%rdi");
        if (isCompare(n->op)) {
            compare(n->op, a->type);
            return;
        }
        if (isPtr(a->type) && isPtr(b->type)) {
            e("subq %rdi, %rax");
            e("cqto");
            e("movq $" + std::to_string(n->scale) + ", %rdi");
            e("idivq %rdi");
            return;
        }
        arith(n->op, isPtr(a->type) ? a->type : n->type, n->scale);
    }

    void genUnary(Node* n) {
        Node* a = n->kids[0];
        gen(a);
        switch (n->op) {
            case Op::Pos:
                return;
            case Op::Neg:
                if (a->type->k == TK::Float) {
                    e("movd %xmm0, %eax");
                    e("xorl $2147483648, %eax");
                    e("movd %eax, %xmm0");
                } else if (a->type->k == TK::Double) {
                    e("movq %xmm0, %rax");
                    e("btcq $63, %rax");
                    e("movq %rax, %xmm0");
                } else {
                    e("negq %rax");
                    normalize(n->type);
                }
                return;
            case Op::BitNot:
                e("notq %rax");
                normalize(n->type);
                return;
            case Op::Not:
                toBool(a->type);
                e("xorq $1, %rax");
                return;
            default:
                return;
        }
    }

    void genCompound(Node* n) {
        Node* lhs = n->kids[0];
        genAddr(lhs);
        push();
        loadFromRax(lhs->type);
        genCast(lhs->type, n->opType);
        pushValue(n->opType);
        gen(n->kids[1]);
        if (isFp(n->opType)) {
            e("movaps %xmm0, %xmm1");
            popf(0);
        } else {
            e("movq %rax, %rdi");
            pop("%rax");
        }
        arith(n->op, n->opType, n->scale);
        genCast(n->opType, n->type);
        pop("%rdi");
        storeToRdi(n->type);
    }

    void genIncDec(Node* n) {
        Node* lhs = n->kids[0];
        Type* t = lhs->type;
        bool inc = n->kind == NK::PreInc || n->kind == NK::PostInc;
        bool pre = n->kind == NK::PreInc || n->kind == NK::PreDec;
        genAddr(lhs);
        push();
        loadFromRax(t);
        if (isFp(t)) {
            pushf();
            const char* s = t->k == TK::Float ? "ss" : "sd";
            e(std::string("movl $1, %eax"));
            e(std::string("cvtsi2") + s + "l %eax, %xmm1");
            e(std::string(inc ? "add" : "sub") + s + " %xmm1, %xmm0");
            if (pre) {
                popf(1);
            } else {
                e("movaps %xmm0, %xmm1");
                popf(0);
                e("movaps %xmm0, %xmm2");
                e("movaps %xmm1, %xmm0");
                e("movaps %xmm2, %xmm1");
            }
            pop("%rdi");
            e(std::string("mov") + s + " %xmm0, (%rdi)");
            if (!pre) e("movaps %xmm1, %xmm0");
            return;
        }
        push();
        long delta = t->k == TK::Bool ? 1 : n->scale;
        e("addq $" + std::to_string(inc ? delta : -delta) + ", %rax");
        normalize(t);
        pop("%rsi");
        pop("%rdi");
        storeToRdi(t);
        if (!pre) e("movq %rsi, %rax");
    }

    void localInits(Node* decl) {
        for (size_t i = 1; i < decl->kids.size(); i++) {
            Node* v = decl->kids[i];
            if (!v->sym || v->sym->kind != SymKind::Var || v->sym->global || v->inits.empty()) continue;
            long off = v->sym->offset;
            if (isAgg(v->sym->type)) {
                e("leaq " + local(off) + ", %rdi");
                e("movq $" + std::to_string(sizeOf(v->sym->type)) + ", %rcx");
                e("xorl %eax, %eax");
                e("rep stosb");
            }
            for (const InitOp& op : v->inits) {
                if (!op.expr) {
                    e("movb $" + std::to_string(op.byte) + ", " + local(off + op.offset));
                    continue;
                }
                gen(op.expr);
                e("leaq " + local(off + op.offset) + ", %rdi");
                storeToRdi(op.type);
            }
        }
    }

    void condJumpFalse(Node* c, const std::string& target) {
        gen(c);
        toBool(c->type);
        e("testq %rax, %rax");
        e("je " + target);
    }

    void collectCases(Node* sw) {
        for (Node* c : sw->cases) caseLabels[c] = label();
        if (sw->target) caseLabels[sw->target] = label();
    }

    void stmt(Node* n) {
        if (!n) return;
        switch (n->kind) {
            case NK::Block:
                for (Node* k : n->kids) {
                    if (k->kind == NK::Declaration) localInits(k);
                    else stmt(k);
                }
                return;
            case NK::ExprStmt:
                gen(n->kids[0]);
                return;
            case NK::If: {
                std::string otherwise = label(), end = label();
                condJumpFalse(n->kids[0], otherwise);
                stmt(n->kids[1]);
                e("jmp " + end);
                place(otherwise);
                stmt(n->kids[2]);
                place(end);
                return;
            }
            case NK::While: {
                std::string begin = label(), end = label();
                place(begin);
                condJumpFalse(n->kids[0], end);
                breaks.push_back(end);
                continues.push_back(begin);
                stmt(n->kids[1]);
                breaks.pop_back();
                continues.pop_back();
                e("jmp " + begin);
                place(end);
                return;
            }
            case NK::DoWhile: {
                std::string begin = label(), test = label(), end = label();
                place(begin);
                breaks.push_back(end);
                continues.push_back(test);
                stmt(n->kids[0]);
                breaks.pop_back();
                continues.pop_back();
                place(test);
                condJumpFalse(n->kids[1], end);
                e("jmp " + begin);
                place(end);
                return;
            }
            case NK::For: {
                std::string begin = label(), step = label(), end = label();
                if (n->kids[0]) {
                    if (n->kids[0]->kind == NK::Declaration) localInits(n->kids[0]);
                    else gen(n->kids[0]);
                }
                place(begin);
                if (n->kids[1]) condJumpFalse(n->kids[1], end);
                breaks.push_back(end);
                continues.push_back(step);
                stmt(n->kids[3]);
                breaks.pop_back();
                continues.pop_back();
                place(step);
                if (n->kids[2]) gen(n->kids[2]);
                e("jmp " + begin);
                place(end);
                return;
            }
            case NK::Switch: {
                std::string end = label();
                collectCases(n);
                gen(n->kids[0]);
                for (Node* c : n->cases) {
                    e("movabsq $" + std::to_string(c->ival) + ", %rdi");
                    e("cmpq %rdi, %rax");
                    e("je " + caseLabels[c]);
                }
                e("jmp " + (n->target ? caseLabels[n->target] : end));
                breaks.push_back(end);
                stmt(n->kids[1]);
                breaks.pop_back();
                place(end);
                return;
            }
            case NK::Case:
                place(caseLabels[n]);
                stmt(n->kids[1]);
                return;
            case NK::Default:
                place(caseLabels[n]);
                stmt(n->kids[0]);
                return;
            case NK::Break:
                e("jmp " + breaks.back());
                return;
            case NK::Continue:
                e("jmp " + continues.back());
                return;
            case NK::Return:
                if (n->kids[0]) {
                    gen(n->kids[0]);
                    if (n->type->k == TK::Struct) {
                        e("movq " + local(cur->hiddenRet) + ", %rdi");
                        storeToRdi(n->type);
                    }
                }
                e("jmp " + retLabel);
                return;
            case NK::Goto:
                e("jmp .Luser." + cur->sym->label + "." + n->text);
                return;
            case NK::Label:
                place(".Luser." + cur->sym->label + "." + n->text);
                stmt(n->kids[0]);
                return;
            default:
                return;
        }
    }

    void storeParam(Symbol* p) {
        std::string dst = local(p->offset);
        switch (sizeOf(p->type)) {
            case 1: e("movb %al, " + dst); break;
            case 2: e("movw %ax, " + dst); break;
            case 4: e("movl %eax, " + dst); break;
            default: e("movq %rax, " + dst);
        }
    }

    void emitFunction(FuncInfo* f) {
        cur = f;
        Symbol* s = f->sym;
        retLabel = label();
        total = (f->frameSize + 48 + 15) / 16 * 16;
        localBase = -total + 48;
        depth = 0;
        if (!s->internal) e(".globl " + s->label);
        e(".type " + s->label + ", @function");
        place(s->label);
        e("pushq %rbp");
        e("movq %rsp, %rbp");
        e("subq $" + std::to_string(total) + ", %rsp");
        for (int i = 0; i < 6; i++) e(std::string("movq ") + GP64[i] + ", " + std::to_string(-total + i * 8) + "(%rbp)");
        int gp = 0, fp = 0, stackIdx = 0;
        if (f->hiddenRet >= 0) {
            e("movq " + std::to_string(-total) + "(%rbp), %rax");
            e("movq %rax, " + local(f->hiddenRet));
            gp++;
        }
        for (Symbol* p : f->params) {
            std::string src;
            if (isFp(p->type)) {
                if (fp < 8) {
                    e(std::string(p->type->k == TK::Float ? "movss" : "movsd") + " %xmm" + std::to_string(fp) + ", " + local(p->offset));
                    fp++;
                    continue;
                }
                src = std::to_string(16 + 8 * stackIdx++) + "(%rbp)";
            } else if (gp < 6) {
                src = std::to_string(-total + 8 * gp++) + "(%rbp)";
            } else {
                src = std::to_string(16 + 8 * stackIdx++) + "(%rbp)";
            }
            e("movq " + src + ", %rax");
            if (p->type->k == TK::Struct) {
                e("movq %rax, %rsi");
                e("leaq " + local(p->offset) + ", %rdi");
                e("movq $" + std::to_string(sizeOf(p->type)) + ", %rcx");
                e("rep movsb");
            } else {
                storeParam(p);
            }
        }
        stmt(f->body);
        if (s->name == "main") e("movl $0, %eax");
        place(retLabel);
        e("leave");
        e("ret");
        e(".size " + s->label + ", .-" + s->label);
    }
};

}

std::string generateAsm(Program& prog) {
    CodeGen g(prog);
    return g.run();
}
