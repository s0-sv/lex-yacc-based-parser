%{
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "ast.hpp"
#include "parser.hpp"

static int curLine = 1;
static int curCol = 1;

struct CondFrame {
    bool parentActive;
    bool active;
    bool taken;
};

static std::map<std::string, std::string> macros;
static std::vector<std::string> macroStack;
static std::vector<CondFrame> conds;

static bool condActive() {
    return conds.empty() || conds.back().active;
}

static void advance(const char* s, int n) {
    if (!macroStack.empty()) return;
    for (int i = 0; i < n; i++) {
        if (s[i] == '\n') {
            curLine++;
            curCol = 1;
        } else {
            curCol++;
        }
    }
}

#define YY_USER_ACTION                     \
    yylloc.first_line = curLine;           \
    yylloc.first_column = curCol;          \
    advance(yytext, yyleng);               \
    yylloc.last_line = curLine;            \
    yylloc.last_column = curCol;

static void logToken(const char* kind) {
    tokenLog.push_back({kind, yytext, yylloc.first_line, yylloc.first_column});
}

static int kw(int token) {
    logToken("keyword");
    return token;
}

static int op(int token) {
    logToken("operator");
    return token;
}

static Node* leaf(NK k, const std::string& text) {
    return mkNode(k, yylloc.first_line, yylloc.first_column, yylloc.last_line, yylloc.last_column, {}, text);
}

static int decodeEscape(const char*& p) {
    char c = *p++;
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'v': return '\v';
        case 'e': return 27;
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case '?': return '?';
        case 'x': {
            int v = 0;
            while (isxdigit((unsigned char)*p)) {
                int d = *p <= '9' ? *p - '0' : (tolower(*p) - 'a' + 10);
                v = v * 16 + d;
                p++;
            }
            return v & 255;
        }
        default:
            if (c >= '0' && c <= '7') {
                int v = c - '0';
                for (int i = 0; i < 2 && *p >= '0' && *p <= '7'; i++) v = v * 8 + (*p++ - '0');
                return v & 255;
            }
            addError(yylloc.first_line, yylloc.first_column, std::string("невідома escape-послідовність \\") + c);
            return c;
    }
}

static std::string decodeBody(const char* text, int len) {
    std::string out;
    const char* p = text + 1;
    const char* end = text + len - 1;
    while (p < end) {
        if (*p == '\\') {
            p++;
            if (*p == '\n') {
                p++;
                continue;
            }
            out.push_back((char)decodeEscape(p));
        } else {
            out.push_back(*p++);
        }
    }
    return out;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string stripLineNoise(const std::string& raw) {
    std::string out;
    bool inStr = false;
    for (size_t i = 0; i < raw.size(); i++) {
        char c = raw[i];
        if (inStr) {
            out.push_back(c);
            if (c == '\\' && i + 1 < raw.size()) out.push_back(raw[++i]);
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') {
            inStr = true;
            out.push_back(c);
        } else if (c == '/' && i + 1 < raw.size() && raw[i + 1] == '/') {
            break;
        } else if (c == '/' && i + 1 < raw.size() && raw[i + 1] == '*') {
            size_t e = raw.find("*/", i + 2);
            if (e == std::string::npos) break;
            out.push_back(' ');
            i = e + 1;
        } else if (c == '\\' && i + 1 < raw.size() && raw[i + 1] == '\n') {
            out.push_back(' ');
            i++;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static void handleDirective(const char* text) {
    int line = yylloc.first_line, col = yylloc.first_column;
    std::string s = trim(stripLineNoise(text));
    s = trim(s.substr(1));
    size_t i = 0;
    while (i < s.size() && isalpha((unsigned char)s[i])) i++;
    std::string name = s.substr(0, i);
    std::string rest = trim(s.substr(i));

    if (name == "ifdef" || name == "ifndef" || name == "if") {
        bool parent = condActive();
        bool value = false;
        if (name == "if") {
            if (rest == "0") value = false;
            else if (rest == "1") value = true;
            else addError(line, col, "#if підтримує лише константи 0 та 1");
        } else {
            value = macros.count(rest) > 0;
            if (name == "ifndef") value = !value;
        }
        conds.push_back({parent, parent && value, value});
        return;
    }
    if (name == "else") {
        if (conds.empty()) {
            addError(line, col, "#else без відповідного #if");
            return;
        }
        CondFrame& f = conds.back();
        f.active = f.parentActive && !f.taken;
        f.taken = true;
        return;
    }
    if (name == "endif") {
        if (conds.empty()) addError(line, col, "#endif без відповідного #if");
        else conds.pop_back();
        return;
    }
    if (!condActive()) return;
    if (name == "include") {
        if (!rest.empty() && rest[0] == '"') addWarning(line, col, "локальний #include проігноровано: " + rest);
        return;
    }
    if (name == "pragma") return;
    if (name == "undef") {
        macros.erase(rest);
        return;
    }
    if (name == "define") {
        size_t j = 0;
        while (j < rest.size() && (isalnum((unsigned char)rest[j]) || rest[j] == '_')) j++;
        std::string macro = rest.substr(0, j);
        if (macro.empty()) {
            addError(line, col, "очікувалося ім'я макросу");
            return;
        }
        if (j < rest.size() && rest[j] == '(') {
            addError(line, col, "макроси з параметрами не підтримуються: " + macro);
            return;
        }
        macros[macro] = trim(rest.substr(j));
        return;
    }
    if (name == "error") {
        addError(line, col, "#error " + rest);
        return;
    }
    addError(line, col, "невідома директива препроцесора: #" + name);
}

static bool expandMacro(const char* name) {
    auto it = macros.find(name);
    if (it == macros.end()) return false;
    for (const auto& m : macroStack) {
        if (m == name) return false;
    }
    std::string body = it->second;
    macroStack.push_back(name);
    yypush_buffer_state(YY_CURRENT_BUFFER);
    yy_scan_string(body.c_str());
    return true;
}

void definePredefinedMacros() {
    macros["NULL"] = "((void*)0)";
    macros["EOF"] = "(-1)";
    macros["true"] = "1";
    macros["false"] = "0";
    macros["RAND_MAX"] = "2147483647";
    macros["INT_MAX"] = "2147483647";
    macros["INT_MIN"] = "(-2147483647-1)";
    macros["UINT_MAX"] = "4294967295U";
    macros["LONG_MAX"] = "9223372036854775807L";
    macros["LONG_MIN"] = "(-9223372036854775807L-1)";
    macros["CHAR_BIT"] = "8";
    macros["M_PI"] = "3.14159265358979323846";
    macros["M_E"] = "2.7182818284590452354";
    macros["EXIT_SUCCESS"] = "0";
    macros["EXIT_FAILURE"] = "1";
}
%}

%option noyywrap nodefault nounput noinput
%x COMMENT SKIP

D   [0-9]
L   [a-zA-Z_]
H   [0-9a-fA-F]
E   [Ee][+-]?{D}+
IS  [uUlL]*
FS  [fFlL]?

%%

<INITIAL,SKIP>^[ \t]*#([^\n\\]|\\(.|\n))*  {
    handleDirective(yytext);
    BEGIN(condActive() ? INITIAL : SKIP);
}

<SKIP>[^\n#]+    { }
<SKIP>#         { }
<SKIP>\n        { }
<SKIP><<EOF>>   {
    addError(curLine, curCol, "незакритий блок умовної компіляції");
    conds.clear();
    BEGIN(INITIAL);
    yyterminate();
}

"/*"            { BEGIN(COMMENT); }
<COMMENT>"*/"   { BEGIN(INITIAL); }
<COMMENT>\n     { }
<COMMENT>.      { }
<COMMENT><<EOF>> {
    addError(curLine, curCol, "незакритий коментар");
    BEGIN(INITIAL);
    yyterminate();
}
"//"[^\n]*      { }

"void"      { return kw(T_VOID); }
"char"      { return kw(T_CHAR); }
"short"     { return kw(T_SHORT); }
"int"       { return kw(T_INT); }
"long"      { return kw(T_LONG); }
"float"     { return kw(T_FLOAT); }
"double"    { return kw(T_DOUBLE); }
"signed"    { return kw(T_SIGNED); }
"unsigned"  { return kw(T_UNSIGNED); }
"_Bool"     { return kw(T_BOOL); }
"bool"      { return kw(T_BOOL); }
"struct"    { return kw(T_STRUCT); }
"union"     { return kw(T_UNION); }
"enum"      { return kw(T_ENUM); }
"typedef"   { return kw(T_TYPEDEF); }
"extern"    { return kw(T_EXTERN); }
"static"    { return kw(T_STATIC); }
"auto"      { return kw(T_AUTO); }
"register"  { return kw(T_REGISTER); }
"inline"    { return kw(T_INLINE); }
"const"     { return kw(T_CONST); }
"volatile"  { return kw(T_VOLATILE); }
"restrict"  { return kw(T_RESTRICT); }
"if"        { return kw(T_IF); }
"else"      { return kw(T_ELSE); }
"while"     { return kw(T_WHILE); }
"do"        { return kw(T_DO); }
"for"       { return kw(T_FOR); }
"switch"    { return kw(T_SWITCH); }
"case"      { return kw(T_CASE); }
"default"   { return kw(T_DEFAULT); }
"break"     { return kw(T_BREAK); }
"continue"  { return kw(T_CONTINUE); }
"return"    { return kw(T_RETURN); }
"goto"      { return kw(T_GOTO); }
"sizeof"    { return kw(T_SIZEOF); }

{L}({L}|{D})*   {
    if (expandMacro(yytext)) continue;
    yylval.str = new std::string(yytext);
    if (isTypedefName(yytext)) {
        logToken("typename");
        return T_TYPE_NAME;
    }
    logToken("identifier");
    return T_IDENT;
}

0[xX]{H}+{IS}       |
0[bB][01]+{IS}      |
{D}+{IS}            { logToken("integer"); yylval.node = leaf(NK::IntLit, yytext); return T_INT_LIT; }

{D}+{E}{FS}             |
{D}*"."{D}+({E})?{FS}   |
{D}+"."{D}*({E})?{FS}   { logToken("float"); yylval.node = leaf(NK::FloatLit, yytext); return T_FLOAT_LIT; }

{D}+({L}|{D})+  {
    addError(yylloc.first_line, yylloc.first_column, std::string("некоректне число: ") + yytext);
    yylval.node = leaf(NK::IntLit, "0");
    return T_INT_LIT;
}

\'([^'\\\n]|\\.)+\'  {
    logToken("char");
    std::string v = decodeBody(yytext, yyleng);
    if (v.size() != 1) addError(yylloc.first_line, yylloc.first_column, "символьна константа повинна містити один символ");
    Node* n = leaf(NK::CharLit, yytext);
    n->ival = v.empty() ? 0 : (long)(signed char)v[0];
    yylval.node = n;
    return T_CHAR_LIT;
}
\'  {
    addError(yylloc.first_line, yylloc.first_column, "незакрита символьна константа");
}

\"([^"\\\n]|\\(.|\n))*\"  {
    logToken("string");
    yylval.node = leaf(NK::StrLit, decodeBody(yytext, yyleng));
    return T_STR_LIT;
}
\"  {
    addError(yylloc.first_line, yylloc.first_column, "незакритий рядковий літерал");
}

"..."   { return op(T_ELLIPSIS); }
"<<="   { return op(T_SHL_ASSIGN); }
">>="   { return op(T_SHR_ASSIGN); }
"+="    { return op(T_ADD_ASSIGN); }
"-="    { return op(T_SUB_ASSIGN); }
"*="    { return op(T_MUL_ASSIGN); }
"/="    { return op(T_DIV_ASSIGN); }
"%="    { return op(T_MOD_ASSIGN); }
"&="    { return op(T_AND_ASSIGN); }
"^="    { return op(T_XOR_ASSIGN); }
"|="    { return op(T_OR_ASSIGN); }
"<<"    { return op(T_SHL); }
">>"    { return op(T_SHR); }
"++"    { return op(T_INC); }
"--"    { return op(T_DEC); }
"->"    { return op(T_ARROW); }
"&&"    { return op(T_LAND); }
"||"    { return op(T_LOR); }
"<="    { return op(T_LE); }
">="    { return op(T_GE); }
"=="    { return op(T_EQ); }
"!="    { return op(T_NE); }
[;{},:=()\[\].&!~+\-*/%<>^|?]  { return op(yytext[0]); }

[ \t\r\f\v\n]+  { }

.   {
    addError(yylloc.first_line, yylloc.first_column, std::string("неочікуваний символ '") + yytext + "'");
}

<<EOF>> {
    if (!macroStack.empty()) {
        macroStack.pop_back();
        yypop_buffer_state();
        continue;
    }
    if (!conds.empty()) {
        addError(curLine, curCol, "незакритий блок умовної компіляції");
        conds.clear();
    }
    yyterminate();
}

%%
