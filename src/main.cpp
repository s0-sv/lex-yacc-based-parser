#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include "ast.hpp"

extern FILE* yyin;
int yyparse();
void definePredefinedMacros();

namespace {

enum class Mode { Run, Ast, AstJson, Tokens, Check, Asm, Build, Json };

struct Options {
    Mode mode = Mode::Run;
    std::string input;
    std::string output;
    long maxSteps = 2000000000L;
    bool execute = true;
};

Options opts;
int exitStatus = 0;

void usage() {
    fputs(
        "використання: minicc [режим] [опції] файл.c\n"
        "  --run            виконати програму інтерпретатором (типово)\n"
        "  --ast            надрукувати AST як дерево\n"
        "  --ast-json       надрукувати AST у форматі JSON\n"
        "  --tokens         надрукувати потік токенів\n"
        "  --check          лише перевірити синтаксис і семантику\n"
        "  -S               згенерувати асемблер x86-64 (GNU as)\n"
        "  --build          зібрати виконуваний файл через gcc\n"
        "  --json           усі результати одним JSON-об'єктом\n"
        "  --no-exec        у режимі --json не виконувати програму\n"
        "  -o файл          вихідний файл для -S і --build\n"
        "  --max-steps N    ліміт кроків інтерпретатора\n"
        "Файл '-' означає стандартний ввід.\n",
        stderr);
}

void printDiagnostics() {
    for (const auto& d : diags) {
        fprintf(stderr, "%s:%d:%d: %s: %s\n", opts.input.c_str(), d.line, d.col, d.error ? "помилка" : "попередження", d.msg.c_str());
    }
}

void writeFile(const std::string& path, const std::string& content) {
    FILE* f = path.empty() || path == "-" ? stdout : fopen(path.c_str(), "w");
    if (!f) {
        perror(path.c_str());
        exit(1);
    }
    fwrite(content.data(), 1, content.size(), f);
    if (f != stdout) fclose(f);
}

int buildExecutable(const std::string& assembly) {
    char tmp[] = "/tmp/miniccXXXXXX.s";
    int fd = mkstemps(tmp, 2);
    if (fd < 0) {
        perror("mkstemps");
        return 1;
    }
    FILE* f = fdopen(fd, "w");
    fwrite(assembly.data(), 1, assembly.size(), f);
    fclose(f);
    std::string out = opts.output.empty() ? "a.out" : opts.output;
    pid_t pid = fork();
    if (pid == 0) {
        execlp("gcc", "gcc", tmp, "-o", out.c_str(), "-lm", (char*)nullptr);
        perror("gcc");
        _exit(127);
    }
    int status = 1;
    waitpid(pid, &status, 0);
    unlink(tmp);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

std::string tokensText() {
    std::ostringstream o;
    for (const auto& t : tokenLog) o << t.line << ":" << t.col << "\t" << t.kind << "\t" << t.text << "\n";
    return o.str();
}

void printJson(Program& prog, bool parsed, bool semanticOk) {
    std::ostringstream o;
    o << "{\"ok\":" << (hasErrors() ? "false" : "true");
    o << ",\"ast\":" << (parsed && parseRoot ? astToJson(parseRoot) : "null");
    o << ",\"tokens\":[";
    for (size_t i = 0; i < tokenLog.size(); i++) {
        const auto& t = tokenLog[i];
        if (i) o << ",";
        o << "{\"kind\":\"" << t.kind << "\",\"text\":\"" << jsonEscape(t.text) << "\",\"line\":" << t.line << ",\"col\":" << t.col << "}";
    }
    o << "],\"diagnostics\":[";
    for (size_t i = 0; i < diags.size(); i++) {
        const auto& d = diags[i];
        if (i) o << ",";
        o << "{\"line\":" << d.line << ",\"col\":" << d.col << ",\"severity\":\"" << (d.error ? "error" : "warning") << "\",\"message\":\"" << jsonEscape(d.msg) << "\"}";
    }
    o << "]";
    if (semanticOk) {
        o << ",\"asm\":\"" << jsonEscape(generateAsm(prog)) << "\"";
        if (opts.execute) {
            RunResult r = interpret(prog, opts.maxSteps, true);
            o << ",\"run\":{\"stdout\":\"" << jsonEscape(r.captured) << "\",\"exit\":" << r.exitCode << ",\"failed\":" << (r.failed ? "true" : "false")
              << ",\"error\":\"" << jsonEscape(r.error) << "\",\"line\":" << r.errorLine << ",\"steps\":" << r.steps << "}";
        }
    }
    o << "}\n";
    std::string s = o.str();
    fwrite(s.data(), 1, s.size(), stdout);
}

void* work(void*) {
    FILE* f = opts.input == "-" ? stdin : fopen(opts.input.c_str(), "r");
    if (!f) {
        perror(opts.input.c_str());
        exitStatus = 1;
        return nullptr;
    }
    definePredefinedMacros();
    yyin = f;
    yyparse();
    if (f != stdin) fclose(f);

    bool syntaxOk = !hasErrors();
    Program prog;
    if (syntaxOk && parseRoot) analyze(prog);
    bool ok = !hasErrors();

    if (opts.mode == Mode::Json) {
        printJson(prog, syntaxOk && parseRoot != nullptr, ok);
        exitStatus = 0;
        return nullptr;
    }

    printDiagnostics();
    if (opts.mode == Mode::Tokens) {
        writeFile("", tokensText());
        exitStatus = ok ? 0 : 1;
        return nullptr;
    }
    if (opts.mode == Mode::Ast && parseRoot) writeFile("", astToText(parseRoot));
    if (opts.mode == Mode::AstJson && parseRoot) writeFile("", astToJson(parseRoot) + "\n");
    if (!ok) {
        exitStatus = 1;
        return nullptr;
    }
    switch (opts.mode) {
        case Mode::Asm:
            writeFile(opts.output, generateAsm(prog));
            break;
        case Mode::Build:
            exitStatus = buildExecutable(generateAsm(prog));
            break;
        case Mode::Run: {
            RunResult r = interpret(prog, opts.maxSteps, false);
            if (r.failed) {
                fprintf(stderr, "%s:%d: помилка виконання: %s\n", opts.input.c_str(), r.errorLine, r.error.c_str());
                exitStatus = 70;
            } else {
                exitStatus = r.exitCode;
            }
            break;
        }
        default:
            break;
    }
    return nullptr;
}

}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--run") opts.mode = Mode::Run;
        else if (a == "--ast") opts.mode = Mode::Ast;
        else if (a == "--ast-json") opts.mode = Mode::AstJson;
        else if (a == "--tokens") opts.mode = Mode::Tokens;
        else if (a == "--check") opts.mode = Mode::Check;
        else if (a == "-S") opts.mode = Mode::Asm;
        else if (a == "--build") opts.mode = Mode::Build;
        else if (a == "--json") opts.mode = Mode::Json;
        else if (a == "--no-exec") opts.execute = false;
        else if (a == "-o" && i + 1 < argc) opts.output = argv[++i];
        else if (a == "--max-steps" && i + 1 < argc) opts.maxSteps = atol(argv[++i]);
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a == "-" || a[0] != '-') {
            opts.input = a;
        } else {
            fprintf(stderr, "невідома опція: %s\n", a.c_str());
            usage();
            return 2;
        }
    }
    if (opts.input.empty()) {
        usage();
        return 2;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2048L << 20);
    pthread_t th;
    if (pthread_create(&th, &attr, work, nullptr) != 0) {
        work(nullptr);
    } else {
        pthread_join(th, nullptr);
    }
    fflush(stdout);
    return exitStatus;
}
