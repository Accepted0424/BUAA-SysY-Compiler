```text
CompUnit → {Decl} {FuncDef} MainFuncDef
Decl → ConstDecl | VarDecl
ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'
BType → 'int'
ConstDef → Ident [ '[' ConstExp ']' ] '=' ConstInitVal
ConstInitVal → ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}'
VarDecl → [ 'static' ] BType VarDef { ',' VarDef } ';'
VarDef → Ident [ '[' ConstExp ']' ] | Ident [ '[' ConstExp ']' ] '=' InitVal
InitVal → Exp | '{' [ Exp { ',' Exp } ] '}'
    FIRST(Decl) = {'const', 'static', 'int'}
    FIRST(FuncDef) = {'void', 'int'}
    FIRST(MainFuncDef) = {'int'}
```
```text
FIRST('Exp') = {'ident', '(', 'digits', '+', '-', '!'}

FIRST(LVal) = {Ident}
FIRST(Exp) = {'Exp', ';'}
FIRST(Block) = {'{'}
FIRST(Stmt) = {';', 'if', 'while', 'break', 'continue', 'return', '{', Ident, Number, '(', '+', '-', '!'}
```