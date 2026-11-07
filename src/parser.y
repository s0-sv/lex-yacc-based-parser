%code requires {
#include <string>
#include "ast.hpp"
}

%code {
#include <cstdio>
int yylex();
void yyerror(const char* msg);

static Node* mk(NK k, const YYLTYPE& l, std::initializer_list<Node*> kids = {}, const std::string& text = "") {
    return mkNode(k, l.first_line, l.first_column, l.last_line, l.last_column, kids, text);
}

static Node* withOp(Node* n, Op op) {
    n->op = op;
    return n;
}

static Node* bin(const YYLTYPE& l, const char* s, Node* a, Node* b) {
    return withOp(mk(NK::Binary, l, {a, b}, s), binaryOpFrom(s));
}

static Node* attach(Node* outer, Node* inner) {
    Node* p = outer;
    while (p->kids[0]) p = p->kids[0];
    p->kids[0] = inner;
    return outer;
}

static Node* adopt(Node* parent, Node* kid) {
    parent->kids.push_back(kid);
    parent->eline = kid->eline;
    parent->ecol = kid->ecol;
    return parent;
}
}

%locations
%define parse.error detailed
%expect 0

%union {
    Node* node;
    std::string* str;
}

%token <str> T_IDENT "ідентифікатор" T_TYPE_NAME "ім'я типу"
%token <node> T_INT_LIT "ціла константа" T_FLOAT_LIT "дійсна константа" T_CHAR_LIT "символьна константа" T_STR_LIT "рядок"
%token T_VOID "void" T_CHAR "char" T_SHORT "short" T_INT "int" T_LONG "long" T_FLOAT "float" T_DOUBLE "double"
%token T_SIGNED "signed" T_UNSIGNED "unsigned" T_BOOL "_Bool" T_STRUCT "struct" T_UNION "union" T_ENUM "enum"
%token T_TYPEDEF "typedef" T_EXTERN "extern" T_STATIC "static" T_AUTO "auto" T_REGISTER "register" T_INLINE "inline"
%token T_CONST "const" T_VOLATILE "volatile" T_RESTRICT "restrict"
%token T_IF "if" T_ELSE "else" T_WHILE "while" T_DO "do" T_FOR "for" T_SWITCH "switch" T_CASE "case"
%token T_DEFAULT "default" T_BREAK "break" T_CONTINUE "continue" T_RETURN "return" T_GOTO "goto" T_SIZEOF "sizeof"
%token T_ELLIPSIS "..." T_SHL_ASSIGN "<<=" T_SHR_ASSIGN ">>=" T_ADD_ASSIGN "+=" T_SUB_ASSIGN "-=" T_MUL_ASSIGN "*="
%token T_DIV_ASSIGN "/=" T_MOD_ASSIGN "%=" T_AND_ASSIGN "&=" T_XOR_ASSIGN "^=" T_OR_ASSIGN "|="
%token T_SHL "<<" T_SHR ">>" T_INC "++" T_DEC "--" T_ARROW "->" T_LAND "&&" T_LOR "||"
%token T_LE "<=" T_GE ">=" T_EQ "==" T_NE "!="

%precedence LOWER_THAN_ELSE
%precedence T_ELSE

%type <node> translation_unit external_declaration function_definition declaration
%type <node> declaration_specifiers specifier init_declarator_list init_declarator
%type <node> struct_or_union struct_specifier field_list field_declaration field_declarator_list field_declarator
%type <node> enum_specifier enumerator_list enumerator
%type <node> declarator direct_declarator pointer parameter_list parameter_type_list parameter_declaration
%type <node> abstract_declarator direct_abstract_declarator type_name
%type <node> initializer initializer_list initializer_item designator_list designator
%type <node> statement labeled_statement compound_statement block_items block_item expression_statement
%type <node> selection_statement iteration_statement jump_statement
%type <node> primary_expression string_literal postfix_expression argument_list unary_expression cast_expression
%type <node> multiplicative_expression additive_expression shift_expression relational_expression
%type <node> equality_expression and_expression xor_expression or_expression logical_and_expression
%type <node> logical_or_expression conditional_expression assignment_expression expression constant_expression
%type <node> opt_expression
%type <str> tag_name

%start program

%%

program
    : %empty { parseRoot = mkNode(NK::TranslationUnit, 1, 1, 1, 1); }
    | translation_unit { parseRoot = $1; }
    ;

translation_unit
    : external_declaration {
        $$ = mk(NK::TranslationUnit, @$);
        if ($1) $$->kids.push_back($1);
    }
    | translation_unit external_declaration {
        $$ = $1;
        if ($2) adopt($$, $2);
    }
    ;

external_declaration
    : function_definition { $$ = $1; }
    | declaration { $$ = $1; }
    | ';' { $$ = nullptr; }
    | error ';' { yyerrok; $$ = nullptr; }
    | error '}' { yyerrok; $$ = nullptr; }
    ;

function_definition
    : declaration_specifiers declarator compound_statement {
        $$ = mk(NK::FunctionDef, @$, {$1, $2, $3});
    }
    ;

declaration
    : declaration_specifiers ';' {
        $$ = mk(NK::Declaration, @$, {$1});
    }
    | declaration_specifiers init_declarator_list ';' {
        $$ = $2;
        $$->kids.insert($$->kids.begin(), $1);
        $$->line = @$.first_line;
        $$->col = @$.first_column;
        $$->eline = @$.last_line;
        $$->ecol = @$.last_column;
        collectTypedefs($$);
    }
    ;

declaration_specifiers
    : specifier { $$ = mk(NK::Specifiers, @$, {$1}); }
    | declaration_specifiers specifier { $$ = adopt($1, $2); }
    ;

specifier
    : T_VOID { $$ = mk(NK::Spec, @$, {}, "void"); }
    | T_CHAR { $$ = mk(NK::Spec, @$, {}, "char"); }
    | T_SHORT { $$ = mk(NK::Spec, @$, {}, "short"); }
    | T_INT { $$ = mk(NK::Spec, @$, {}, "int"); }
    | T_LONG { $$ = mk(NK::Spec, @$, {}, "long"); }
    | T_FLOAT { $$ = mk(NK::Spec, @$, {}, "float"); }
    | T_DOUBLE { $$ = mk(NK::Spec, @$, {}, "double"); }
    | T_SIGNED { $$ = mk(NK::Spec, @$, {}, "signed"); }
    | T_UNSIGNED { $$ = mk(NK::Spec, @$, {}, "unsigned"); }
    | T_BOOL { $$ = mk(NK::Spec, @$, {}, "_Bool"); }
    | T_TYPEDEF { $$ = mk(NK::Spec, @$, {}, "typedef"); }
    | T_EXTERN { $$ = mk(NK::Spec, @$, {}, "extern"); }
    | T_STATIC { $$ = mk(NK::Spec, @$, {}, "static"); }
    | T_AUTO { $$ = mk(NK::Spec, @$, {}, "auto"); }
    | T_REGISTER { $$ = mk(NK::Spec, @$, {}, "register"); }
    | T_INLINE { $$ = mk(NK::Spec, @$, {}, "inline"); }
    | T_CONST { $$ = mk(NK::Spec, @$, {}, "const"); }
    | T_VOLATILE { $$ = mk(NK::Spec, @$, {}, "volatile"); }
    | T_RESTRICT { $$ = mk(NK::Spec, @$, {}, "restrict"); }
    | T_TYPE_NAME { $$ = mk(NK::TypedefRef, @$, {}, *$1); delete $1; }
    | struct_specifier { $$ = $1; }
    | enum_specifier { $$ = $1; }
    ;

init_declarator_list
    : init_declarator { $$ = mk(NK::Declaration, @$, {$1}); }
    | init_declarator_list ',' init_declarator { $$ = adopt($1, $3); }
    ;

init_declarator
    : declarator { $$ = mk(NK::VarDecl, @$, {$1, nullptr}); }
    | declarator '=' initializer { $$ = mk(NK::VarDecl, @$, {$1, $3}); }
    ;

tag_name
    : T_IDENT { $$ = $1; }
    | T_TYPE_NAME { $$ = $1; }
    ;

struct_or_union
    : T_STRUCT { $$ = mk(NK::StructSpec, @$, {}, ""); $$->ival = 0; }
    | T_UNION { $$ = mk(NK::StructSpec, @$, {}, ""); $$->ival = 1; }
    ;

struct_specifier
    : struct_or_union '{' field_list '}' {
        $$ = $1;
        $$->kids = $3->kids;
        $$->scale = 1;
        $$->eline = @$.last_line;
        $$->ecol = @$.last_column;
    }
    | struct_or_union tag_name '{' field_list '}' {
        $$ = $1;
        $$->text = *$2;
        delete $2;
        $$->kids = $4->kids;
        $$->scale = 1;
        $$->eline = @$.last_line;
        $$->ecol = @$.last_column;
    }
    | struct_or_union tag_name {
        $$ = $1;
        $$->text = *$2;
        delete $2;
        $$->scale = 0;
        $$->eline = @$.last_line;
        $$->ecol = @$.last_column;
    }
    ;

field_list
    : field_declaration { $$ = mk(NK::Block, @$, {$1}); }
    | field_list field_declaration { $$ = adopt($1, $2); }
    ;

field_declaration
    : declaration_specifiers ';' { $$ = mk(NK::FieldDecl, @$, {$1}); }
    | declaration_specifiers field_declarator_list ';' {
        $$ = $2;
        $$->kids.insert($$->kids.begin(), $1);
    }
    ;

field_declarator_list
    : field_declarator { $$ = mk(NK::FieldDecl, @$, {$1}); }
    | field_declarator_list ',' field_declarator { $$ = adopt($1, $3); }
    ;

field_declarator
    : declarator { $$ = mk(NK::FieldDeclarator, @$, {$1, nullptr}); }
    | declarator ':' constant_expression { $$ = mk(NK::FieldDeclarator, @$, {$1, $3}); }
    | ':' constant_expression { $$ = mk(NK::FieldDeclarator, @$, {nullptr, $2}); }
    ;

enum_specifier
    : T_ENUM '{' enumerator_list '}' { $$ = $3; }
    | T_ENUM '{' enumerator_list ',' '}' { $$ = $3; }
    | T_ENUM tag_name '{' enumerator_list '}' { $$ = $4; $$->text = *$2; delete $2; }
    | T_ENUM tag_name '{' enumerator_list ',' '}' { $$ = $4; $$->text = *$2; delete $2; }
    | T_ENUM tag_name { $$ = mk(NK::EnumSpec, @$, {}, *$2); delete $2; }
    ;

enumerator_list
    : enumerator { $$ = mk(NK::EnumSpec, @$, {$1}); }
    | enumerator_list ',' enumerator { $$ = adopt($1, $3); }
    ;

enumerator
    : T_IDENT { $$ = mk(NK::Enumerator, @$, {nullptr}, *$1); delete $1; }
    | T_IDENT '=' constant_expression { $$ = mk(NK::Enumerator, @$, {$3}, *$1); delete $1; }
    ;

declarator
    : pointer direct_declarator { $$ = attach($1, $2); }
    | direct_declarator { $$ = $1; }
    ;

direct_declarator
    : T_IDENT { $$ = mk(NK::DeclName, @$, {nullptr}, *$1); delete $1; }
    | '(' declarator ')' { $$ = $2; }
    | direct_declarator '[' ']' { $$ = mk(NK::ArrayDecl, @$, {$1, nullptr}); }
    | direct_declarator '[' constant_expression ']' { $$ = mk(NK::ArrayDecl, @$, {$1, $3}); }
    | direct_declarator '(' ')' { $$ = mk(NK::FuncDecl, @$, {$1}); }
    | direct_declarator '(' parameter_type_list ')' {
        $$ = $3;
        $$->kids[0] = $1;
    }
    ;

pointer
    : '*' { $$ = mk(NK::PointerDecl, @$, {nullptr}); }
    | '*' qualifier_list { $$ = mk(NK::PointerDecl, @$, {nullptr}); }
    | '*' pointer { $$ = mk(NK::PointerDecl, @$, {$2}); }
    | '*' qualifier_list pointer { $$ = mk(NK::PointerDecl, @$, {$3}); }
    ;

qualifier_list
    : qualifier
    | qualifier_list qualifier
    ;

qualifier
    : T_CONST
    | T_VOLATILE
    | T_RESTRICT
    ;

parameter_type_list
    : parameter_list { $$ = $1; }
    | parameter_list ',' T_ELLIPSIS { $$ = $1; $$->ival = 1; }
    ;

parameter_list
    : parameter_declaration { $$ = mk(NK::FuncDecl, @$, {nullptr, $1}); }
    | parameter_list ',' parameter_declaration { $$ = adopt($1, $3); }
    ;

parameter_declaration
    : declaration_specifiers declarator { $$ = mk(NK::ParamDecl, @$, {$1, $2}); }
    | declaration_specifiers abstract_declarator { $$ = mk(NK::ParamDecl, @$, {$1, $2}); }
    | declaration_specifiers { $$ = mk(NK::ParamDecl, @$, {$1, nullptr}); }
    ;

type_name
    : declaration_specifiers { $$ = mk(NK::TypeName, @$, {$1, nullptr}); }
    | declaration_specifiers abstract_declarator { $$ = mk(NK::TypeName, @$, {$1, $2}); }
    ;

abstract_declarator
    : pointer { $$ = $1; }
    | direct_abstract_declarator { $$ = $1; }
    | pointer direct_abstract_declarator { $$ = attach($1, $2); }
    ;

direct_abstract_declarator
    : '(' abstract_declarator ')' { $$ = $2; }
    | '[' ']' { $$ = mk(NK::ArrayDecl, @$, {nullptr, nullptr}); }
    | '[' constant_expression ']' { $$ = mk(NK::ArrayDecl, @$, {nullptr, $2}); }
    | direct_abstract_declarator '[' ']' { $$ = mk(NK::ArrayDecl, @$, {$1, nullptr}); }
    | direct_abstract_declarator '[' constant_expression ']' { $$ = mk(NK::ArrayDecl, @$, {$1, $3}); }
    | '(' ')' { $$ = mk(NK::FuncDecl, @$, {nullptr}); }
    | '(' parameter_type_list ')' { $$ = $2; }
    | direct_abstract_declarator '(' ')' { $$ = mk(NK::FuncDecl, @$, {$1}); }
    | direct_abstract_declarator '(' parameter_type_list ')' { $$ = $3; $$->kids[0] = $1; }
    ;

initializer
    : assignment_expression { $$ = $1; }
    | '{' initializer_list '}' { $$ = $2; }
    | '{' initializer_list ',' '}' { $$ = $2; }
    | '{' '}' { $$ = mk(NK::InitList, @$); }
    ;

initializer_list
    : initializer_item { $$ = mk(NK::InitList, @$, {$1}); }
    | initializer_list ',' initializer_item { $$ = adopt($1, $3); }
    ;

initializer_item
    : initializer { $$ = $1; }
    | designator_list '=' initializer { $$ = adopt($1, $3); }
    ;

designator_list
    : designator { $$ = mk(NK::Designated, @$, {$1}); }
    | designator_list designator { $$ = adopt($1, $2); }
    ;

designator
    : '.' tag_name { $$ = mk(NK::DesigField, @$, {}, *$2); delete $2; }
    | '[' constant_expression ']' { $$ = mk(NK::DesigIndex, @$, {$2}); }
    ;

statement
    : labeled_statement { $$ = $1; }
    | compound_statement { $$ = $1; }
    | expression_statement { $$ = $1; }
    | selection_statement { $$ = $1; }
    | iteration_statement { $$ = $1; }
    | jump_statement { $$ = $1; }
    | error ';' { yyerrok; $$ = mk(NK::Empty, @$); }
    ;

labeled_statement
    : T_IDENT ':' statement { $$ = mk(NK::Label, @$, {$3}, *$1); delete $1; }
    | T_CASE constant_expression ':' statement { $$ = mk(NK::Case, @$, {$2, $4}); }
    | T_DEFAULT ':' statement { $$ = mk(NK::Default, @$, {$3}); }
    ;

block_open
    : '{' { scopePush(); }
    ;

compound_statement
    : block_open '}' {
        scopePop();
        $$ = mk(NK::Block, @$);
    }
    | block_open block_items '}' {
        scopePop();
        $$ = $2;
        $$->line = @$.first_line;
        $$->col = @$.first_column;
        $$->eline = @$.last_line;
        $$->ecol = @$.last_column;
    }
    ;

block_items
    : block_item { $$ = mk(NK::Block, @$, {$1}); }
    | block_items block_item { $$ = adopt($1, $2); }
    ;

block_item
    : declaration { $$ = $1; }
    | statement { $$ = $1; }
    ;

expression_statement
    : ';' { $$ = mk(NK::Empty, @$); }
    | expression ';' { $$ = mk(NK::ExprStmt, @$, {$1}); }
    ;

selection_statement
    : T_IF '(' expression ')' statement %prec LOWER_THAN_ELSE { $$ = mk(NK::If, @$, {$3, $5, nullptr}); }
    | T_IF '(' expression ')' statement T_ELSE statement { $$ = mk(NK::If, @$, {$3, $5, $7}); }
    | T_SWITCH '(' expression ')' statement { $$ = mk(NK::Switch, @$, {$3, $5}); }
    ;

iteration_statement
    : T_WHILE '(' expression ')' statement { $$ = mk(NK::While, @$, {$3, $5}); }
    | T_DO statement T_WHILE '(' expression ')' ';' { $$ = mk(NK::DoWhile, @$, {$2, $5}); }
    | T_FOR '(' opt_expression ';' opt_expression ';' opt_expression ')' statement {
        $$ = mk(NK::For, @$, {$3, $5, $7, $9});
    }
    | T_FOR '(' declaration opt_expression ';' opt_expression ')' statement {
        $$ = mk(NK::For, @$, {$3, $4, $6, $8});
    }
    ;

opt_expression
    : %empty { $$ = nullptr; }
    | expression { $$ = $1; }
    ;

jump_statement
    : T_GOTO T_IDENT ';' { $$ = mk(NK::Goto, @$, {}, *$2); delete $2; }
    | T_CONTINUE ';' { $$ = mk(NK::Continue, @$); }
    | T_BREAK ';' { $$ = mk(NK::Break, @$); }
    | T_RETURN ';' { $$ = mk(NK::Return, @$, {nullptr}); }
    | T_RETURN expression ';' { $$ = mk(NK::Return, @$, {$2}); }
    ;

primary_expression
    : T_IDENT { $$ = mk(NK::Ident, @$, {}, *$1); delete $1; }
    | T_INT_LIT { $$ = $1; }
    | T_FLOAT_LIT { $$ = $1; }
    | T_CHAR_LIT { $$ = $1; }
    | string_literal { $$ = $1; }
    | '(' expression ')' { $$ = $2; }
    ;

string_literal
    : T_STR_LIT { $$ = $1; }
    | string_literal T_STR_LIT {
        $$ = $1;
        $$->text += $2->text;
        $$->eline = @$.last_line;
        $$->ecol = @$.last_column;
    }
    ;

postfix_expression
    : primary_expression { $$ = $1; }
    | postfix_expression '[' expression ']' { $$ = mk(NK::Index, @$, {$1, $3}); }
    | postfix_expression '(' ')' { $$ = mk(NK::Call, @$, {$1}); }
    | postfix_expression '(' argument_list ')' {
        $$ = $3;
        $$->kids[0] = $1;
        $$->line = @$.first_line;
        $$->col = @$.first_column;
        $$->eline = @$.last_line;
        $$->ecol = @$.last_column;
    }
    | postfix_expression '.' tag_name {
        $$ = mk(NK::Member, @$, {$1}, *$3);
        $$->scale = 0;
        delete $3;
    }
    | postfix_expression T_ARROW tag_name {
        $$ = mk(NK::Member, @$, {$1}, *$3);
        $$->scale = 1;
        delete $3;
    }
    | postfix_expression T_INC { $$ = mk(NK::PostInc, @$, {$1}, "++"); }
    | postfix_expression T_DEC { $$ = mk(NK::PostDec, @$, {$1}, "--"); }
    ;

argument_list
    : assignment_expression { $$ = mk(NK::Call, @$, {nullptr, $1}); }
    | argument_list ',' assignment_expression { $$ = adopt($1, $3); }
    ;

unary_expression
    : postfix_expression { $$ = $1; }
    | T_INC unary_expression { $$ = mk(NK::PreInc, @$, {$2}, "++"); }
    | T_DEC unary_expression { $$ = mk(NK::PreDec, @$, {$2}, "--"); }
    | '&' cast_expression { $$ = mk(NK::AddrOf, @$, {$2}, "&"); }
    | '*' cast_expression { $$ = mk(NK::Deref, @$, {$2}, "*"); }
    | '+' cast_expression { $$ = withOp(mk(NK::Unary, @$, {$2}, "+"), Op::Pos); }
    | '-' cast_expression { $$ = withOp(mk(NK::Unary, @$, {$2}, "-"), Op::Neg); }
    | '~' cast_expression { $$ = withOp(mk(NK::Unary, @$, {$2}, "~"), Op::BitNot); }
    | '!' cast_expression { $$ = withOp(mk(NK::Unary, @$, {$2}, "!"), Op::Not); }
    | T_SIZEOF unary_expression { $$ = mk(NK::SizeofExpr, @$, {$2}); }
    | T_SIZEOF '(' type_name ')' { $$ = mk(NK::SizeofType, @$, {$3}); }
    ;

cast_expression
    : unary_expression { $$ = $1; }
    | '(' type_name ')' cast_expression { $$ = mk(NK::Cast, @$, {$2, $4}); }
    ;

multiplicative_expression
    : cast_expression { $$ = $1; }
    | multiplicative_expression '*' cast_expression { $$ = bin(@$, "*", $1, $3); }
    | multiplicative_expression '/' cast_expression { $$ = bin(@$, "/", $1, $3); }
    | multiplicative_expression '%' cast_expression { $$ = bin(@$, "%", $1, $3); }
    ;

additive_expression
    : multiplicative_expression { $$ = $1; }
    | additive_expression '+' multiplicative_expression { $$ = bin(@$, "+", $1, $3); }
    | additive_expression '-' multiplicative_expression { $$ = bin(@$, "-", $1, $3); }
    ;

shift_expression
    : additive_expression { $$ = $1; }
    | shift_expression T_SHL additive_expression { $$ = bin(@$, "<<", $1, $3); }
    | shift_expression T_SHR additive_expression { $$ = bin(@$, ">>", $1, $3); }
    ;

relational_expression
    : shift_expression { $$ = $1; }
    | relational_expression '<' shift_expression { $$ = bin(@$, "<", $1, $3); }
    | relational_expression '>' shift_expression { $$ = bin(@$, ">", $1, $3); }
    | relational_expression T_LE shift_expression { $$ = bin(@$, "<=", $1, $3); }
    | relational_expression T_GE shift_expression { $$ = bin(@$, ">=", $1, $3); }
    ;

equality_expression
    : relational_expression { $$ = $1; }
    | equality_expression T_EQ relational_expression { $$ = bin(@$, "==", $1, $3); }
    | equality_expression T_NE relational_expression { $$ = bin(@$, "!=", $1, $3); }
    ;

and_expression
    : equality_expression { $$ = $1; }
    | and_expression '&' equality_expression { $$ = bin(@$, "&", $1, $3); }
    ;

xor_expression
    : and_expression { $$ = $1; }
    | xor_expression '^' and_expression { $$ = bin(@$, "^", $1, $3); }
    ;

or_expression
    : xor_expression { $$ = $1; }
    | or_expression '|' xor_expression { $$ = bin(@$, "|", $1, $3); }
    ;

logical_and_expression
    : or_expression { $$ = $1; }
    | logical_and_expression T_LAND or_expression { $$ = bin(@$, "&&", $1, $3); }
    ;

logical_or_expression
    : logical_and_expression { $$ = $1; }
    | logical_or_expression T_LOR logical_and_expression { $$ = bin(@$, "||", $1, $3); }
    ;

conditional_expression
    : logical_or_expression { $$ = $1; }
    | logical_or_expression '?' expression ':' conditional_expression { $$ = mk(NK::Ternary, @$, {$1, $3, $5}); }
    ;

assignment_expression
    : conditional_expression { $$ = $1; }
    | unary_expression '=' assignment_expression { $$ = mk(NK::Assign, @$, {$1, $3}, "="); }
    | unary_expression T_ADD_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "+="), Op::Add); }
    | unary_expression T_SUB_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "-="), Op::Sub); }
    | unary_expression T_MUL_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "*="), Op::Mul); }
    | unary_expression T_DIV_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "/="), Op::Div); }
    | unary_expression T_MOD_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "%="), Op::Mod); }
    | unary_expression T_SHL_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "<<="), Op::Shl); }
    | unary_expression T_SHR_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, ">>="), Op::Shr); }
    | unary_expression T_AND_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "&="), Op::BitAnd); }
    | unary_expression T_XOR_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "^="), Op::BitXor); }
    | unary_expression T_OR_ASSIGN assignment_expression { $$ = withOp(mk(NK::CompoundAssign, @$, {$1, $3}, "|="), Op::BitOr); }
    ;

expression
    : assignment_expression { $$ = $1; }
    | expression ',' assignment_expression { $$ = mk(NK::Comma, @$, {$1, $3}, ","); }
    ;

constant_expression
    : conditional_expression { $$ = $1; }
    ;

%%

static std::string localize(std::string m) {
    const std::pair<const char*, const char*> table[] = {
        {"syntax error", "синтаксична помилка"},
        {"unexpected end of file", "неочікуваний кінець файлу"},
        {"unexpected", "неочікуваний токен"},
        {"expecting", "очікувалося"},
        {" or ", " або "},
    };
    for (const auto& t : table) {
        size_t pos = 0;
        std::string from = t.first;
        while ((pos = m.find(from, pos)) != std::string::npos) {
            m.replace(pos, from.size(), t.second);
            pos += std::string(t.second).size();
        }
    }
    return m;
}

void yyerror(const char* msg) {
    addError(yylloc.first_line, yylloc.first_column, localize(msg));
}
