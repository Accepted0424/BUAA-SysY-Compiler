#include "parser.h"

#include "ast.h"
#include "error.h"
#include "logger.h"

void Parser::getToken() {
    last_ = token_;
    token_ = lookahead_;
    lexer_.next(lookahead_);
}

void Parser::ungetToken() {
    lookahead_ = token_;
}

bool Parser::match(Token::TokenType expected) {
    if (token_.type == expected) {
        getToken();
        return true;
    }
    ErrorReporter::error(token_.lineno, "[Parser] unexpected token: " + token_.content + ", expected: " + Token::toString(expected));
    return false;
}

bool Parser::is(Token::TokenType type) {
    return token_.type == type;
}

bool Parser::is(Token::TokenType type, Token::TokenType ahead_type) {
    return token_.type == type && lookahead_.type == ahead_type;
}

void Parser::parse() {
    parseCompUnit();
}

/**
 * @brief 解析`标识符`
 */
std::unique_ptr<Ident> Parser::parseIdent() {
    auto ident = std::make_unique<Ident>();

    ident->lineno = token_.lineno;

    ident->content = token_.content;

    match(Token::IDENFR);

    return ident;
}

/**
 * @brief 解析`声明`
 * @note ConstDecl | VarDecl
 */
std::unique_ptr<Decl> Parser::parseDecl() {
    if (is(Token::CONSTTK)) {
        auto constDecl = parseConstDecl();
        return std::make_unique<Decl>(std::move(*constDecl));
    }

    if (is(Token::STATICTK) || is(Token::INTTK)) {
        auto varDecl = parseVarDecl();
        return std::make_unique<Decl>(std::move(*varDecl));
    }

    return nullptr;
}

/**
 * @brief 解析`基本类型`
 * @note 'int'
 */
std::unique_ptr<Btype> Parser::parseBType() {
    auto btype = std::make_unique<Btype>();
    btype->lineno = token_.lineno;

    if (is(Token::INTTK)) {
        match(Token::INTTK);
        btype->type = std::make_unique<std::string>(Token::toString(Token::INTTK));
        return btype;
    }

    return nullptr;
}

/**
 * @brief 解析`常量声明`
 * @note 'const' BType ConstDef { ',' ConstDef } ';'
 */
std::unique_ptr<ConstDecl> Parser::parseConstDecl() {
    auto constDecl = std::make_unique<ConstDecl>();
    constDecl->lineno = token_.lineno;

    match(Token::CONSTTK);

    constDecl->btype = parseBType();

    constDecl->constDefs.push_back(parseConstDef());

    while (is(Token::COMMA)) {
        match(Token::COMMA);
        constDecl->constDefs.push_back(parseConstDef());
    }

    match(Token::SEMICN);

    return constDecl;
}

/**
 * @brief 解析`常量定义`
 * @note Ident [ '[' ConstExp ']' ] '=' ConstInitVal
 */
std::unique_ptr<ConstDef> Parser::parseConstDef() {
    auto constDef = std::make_unique<ConstDef>();
    constDef->lineno = token_.lineno;

    constDef->ident = parseIdent();

    if (is(Token::LBRACK)) {
        match(Token::LBRACK);

        constDef->constExp = parseConstExp();

        match(Token::RBRACK);
    }

    match(Token::EQL);

    constDef->constInitVal = parseConstInitVal();

    return constDef;
}

/**
 * @brief 解析`表达式`
 * @note AddExp
 */
std::unique_ptr<Exp> Parser::parseExp() {
    auto exp = std::make_unique<Exp>();
    exp->lineno = token_.lineno;

    exp->addExp = parseAddExp();

    return exp;
}

/**
 * @brief 解析`左值表达式`
 * @note Ident ['[' Exp ']']
 */
std::unique_ptr<LVal> Parser::parseLVal() {
    auto lval = std::make_unique<LVal>();
    lval->lineno = token_.lineno;

    lval->ident = parseIdent();

    if (is(Token::LBRACK)) {
        match(Token::LBRACK);

        lval->index = parseExp();

        match(Token::RBRACK);
    }

    return lval;
}

/**
 * @brief 解析`数值`
 * @note IntConst
 */
std::unique_ptr<Number> Parser::parseNumber() {
    auto num = std::make_unique<Number>();

    num->lineno = token_.lineno;

    num->value = token_.content;

    match(Token::INTCON);

    return num;
}

/**
 * @brief 解析`基本表达式`
 * @note '(' Exp ')' | LVal | Number
 */
std::unique_ptr<PrimaryExp> Parser::parsePrimaryExp() {
    auto primaryExp = std::make_unique<PrimaryExp>();
    primaryExp->lineno = token_.lineno;

    if (is(Token::LPARENT)) {
        primaryExp->kind = PrimaryExp::EXP;

        match(Token::LPARENT);

        primaryExp->exp = parseExp();

        match(Token::RPARENT);

        return primaryExp;
    }

    if (is(Token::IDENFR)) {
        primaryExp->kind = PrimaryExp::LVAL;

        match(Token::IDENFR);

        primaryExp->lval = parseLVal();

        return primaryExp;
    }

    if (is(Token::INTCON)) {
        primaryExp->kind = PrimaryExp::NUMBER;

        primaryExp->number = parseNumber();

        return primaryExp;
    }

    return nullptr;
}

/**
 * @brief 解析`单目运算符`
 * @note '+' | '−' | '!'
 */
std::unique_ptr<UnaryOp> Parser::parseUnaryOp() {
    auto unaryOp = std::make_unique<UnaryOp>();
    unaryOp->lineno = token_.lineno;

    if (is(Token::PLUS)) {
        unaryOp->kind = UnaryOp::PLUS;
        match(Token::PLUS);
        return unaryOp;
    }

    if (is(Token::MINU)) {
        unaryOp->kind = UnaryOp::MINU;
        match(Token::MINU);
        return unaryOp;
    }

    if (is(Token::NOT)) {
        unaryOp->kind = UnaryOp::NOT;
        match(Token::NOT);
        return unaryOp;
    }

    return nullptr;
}

/**
 * @brief 解析`函数实参表达式`
 * @note  Exp { ',' Exp }
 */
std::unique_ptr<FuncRParams> Parser::parseFuncRParams() {
    auto funcRParams = std::make_unique<FuncRParams>();
    funcRParams->lineno = token_.lineno;

    funcRParams->params.push_back(parseExp());

    while (is(Token::COMMA)) {
        match(Token::COMMA);
        funcRParams->params.push_back(parseExp());
    }

    return funcRParams;
}

/**
 * @brief 解析`一元表达式`
 * @note PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp
 */
std::unique_ptr<UnaryExp> Parser::parseUnaryExp() {
    auto unaryExp = std::make_unique<UnaryExp>();
    unaryExp->lineno = token_.lineno;

    if (is(Token::IDENFR, Token::LPARENT)) {
        unaryExp->kind = UnaryExp::CALL;
        UnaryExp::Call call;

        call.ident = parseIdent();

        match(Token::LPARENT);

        call.params = parseFuncRParams();

        match(Token::RPARENT);

        unaryExp->call = std::make_unique<UnaryExp::Call>(std::move(call));
        return unaryExp;
    }

    if (is(Token::LPARENT) || is(Token::IDENFR) || is(Token::INTCON)) {
        unaryExp->kind = UnaryExp::PRIMARY;

        unaryExp->primary = parsePrimaryExp();

        return unaryExp;
    }

    if (is(Token::PLUS) || is(Token::MINU) || is(Token::NOT)) {
        unaryExp->kind = UnaryExp::UNARY_OP;

        UnaryExp::Unary unary;

        if (is(Token::PLUS)) {
            unary.op = parseUnaryOp();
        } else if (is(Token::MINU)) {
            unary.op = parseUnaryOp();
        } else if (is(Token::NOT)) {
            unary.op = parseUnaryOp();
        } else {
            LOG_ERROR(token_.lineno, "[Parser] invalid UnaryOp in UnaryExp");
        }

        unary.expr = parseUnaryExp();
        unaryExp->unary.push_back(std::move(unary));
        return unaryExp;
    }

    return nullptr;
}

/**
 * @brief 解析`乘除模表达式`
 * @note UnaryExp | MulExp ('*' | '/' | '%') UnaryExp
 */
std::unique_ptr<MulExp> Parser::parseMulExp() {
    auto mulExp = std::make_unique<MulExp>();
    mulExp->lineno = token_.lineno;

    mulExp->first = parseUnaryExp();

    while (is(Token::MULT) || is(Token::DIV) || is(Token::MOD)) {
        MulExp::Operator op;

        if (is(Token::MULT)) {
            op = MulExp::MULT;
            match(Token::MULT);
        } else if (is(Token::DIV)) {
            op = MulExp::DIV;
            match(Token::DIV);
        } else {
            op = MulExp::MOD;
            match(Token::MOD);
        }

        auto unaryExp = parseUnaryExp();

        mulExp->rest.push_back({op, std::move(unaryExp)});
    }

    return mulExp;

}

/**
 * @brief 解析`加减表达式`
 * @note MulExp | AddExp ('+' | '−') MulExp
 */
std::unique_ptr<AddExp> Parser::parseAddExp() {
    auto addExp = std::make_unique<AddExp>();
    addExp->lineno = token_.lineno;

    addExp->first = parseMulExp();

    while (is(Token::PLUS) || is(Token::MINU)) {
        AddExp::Operator op;

        if (is(Token::PLUS)) {
            op = AddExp::PLUS;
            match(Token::PLUS);
        } else {
            op = AddExp::MINU;
            match(Token::MINU);
        }

        auto mulExp = parseMulExp();

        addExp->rest.emplace_back(op, std::move(mulExp));
    }

    return addExp;
}

/**
 * @brief 解析`常量表达式`
 * @note AddExp
 */
std::unique_ptr<ConstExp> Parser::parseConstExp() {
    auto constExp = std::make_unique<ConstExp>();
    constExp->lineno = token_.lineno;

    constExp->addExp = parseAddExp();

    return constExp;
}

/**
 * @brief 解析`常量初值`
 * @note ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}'
 */
std::unique_ptr<ConstInitVal> Parser::parseConstInitVal() {
    auto constInitVal = std::make_unique<ConstInitVal>();
    constInitVal->lineno = token_.lineno;

    if (is(Token::LBRACE)) {
        match(Token::LBRACE);

        constInitVal->kind = ConstInitVal::LIST;

        if (!is(Token::RBRACE)) {
            constInitVal->list.push_back(parseConstExp());

            while (is(Token::COMMA)) {
                match(Token::COMMA);
                constInitVal->list.push_back(parseConstExp());
            }
        }

        match(Token::RBRACE);
    } else {
        constInitVal->kind = ConstInitVal::EXP;
        constInitVal->exp = parseConstExp();
    }

    return constInitVal;
}

/**
 * @brief 解析`变量初值`
 * @note Exp | '{' [ Exp { ',' Exp } ] '}'
 */
std::unique_ptr<InitVal> Parser::parseInitVal() {
    auto initVal = std::make_unique<InitVal>();
    initVal->lineno = token_.lineno;

    if (is(Token::LBRACE)) {
        match(Token::LBRACE);

        initVal->kind = InitVal::LIST;

        if (!is(Token::RBRACE)) {
            initVal->list.push_back(parseExp());

            while (is(Token::COMMA)) {
                match(Token::COMMA);
                initVal->list.push_back(parseExp());
            }
        }

        match(Token::RBRACE);
    } else {
        initVal->kind = InitVal::EXP;
        initVal->exp = parseExp();
    }

    return initVal;
}

/**
 * @brief 解析`变量定义`
 * @note Ident [ '[' ConstExp ']' ] | Ident [ '[' ConstExp ']' ] '=' InitVal
 */
std::unique_ptr<VarDef> Parser::parseVarDef() {
    auto varDef = std::make_unique<VarDef>();
    varDef->lineno = token_.lineno;

    varDef->ident = parseIdent();

    if (is(Token::LBRACK)) {
        match(Token::LBRACK);

        varDef->constExp = parseConstExp();

        match(Token::RBRACK);
    }

    if (is(Token::EQL)) {
        match(Token::EQL);

        varDef->initVal = parseInitVal();
    }

    return varDef;
}

/**
 * @brief 解析`变量声明`
 * @note [ 'static' ] BType VarDef { ',' VarDef } ';'
 */
std::unique_ptr<VarDecl> Parser::parseVarDecl() {
    auto varDecl = std::make_unique<VarDecl>();
    varDecl->lineno = token_.lineno;

    if (is(Token::STATICTK)) {
        varDecl->prefix = Token::toString(Token::STATICTK);
        match(Token::STATICTK);
    } else {
        varDecl->prefix = "";
    }

    varDecl->btype = parseBType();

    varDecl->varDefs.push_back(parseVarDef());

    while (is(Token::COMMA)) {
        match(Token::COMMA);
        varDecl->varDefs.push_back(parseVarDef());
    }

    match (Token::SEMICN);

    return varDecl;
}

bool Parser::parseStmt() {

}

/**
 * @brief 解析`语句块项`
 * @note Decl | Stmt
 */
std::unique_ptr<BlockItem> Parser::parseBlockItem() {

}

/**
 * @brief 解析`语句块`
 * @note '{' { BlockItem } '}'
 */
std::unique_ptr<Block> Parser::parseBlock() {
    auto block = std::make_unique<Block>();

    match(Token::LBRACE);

    while (!is(Token::RBRACE)) {
        block->blockItems.push_back(parseBlockItem());
    }

    match(Token::RBRACE);

    return block;
}

/**
 * @brief 解析`主函数定义`
 * @note 'int' 'main' '(' ')' Block
 */
std::unique_ptr<MainFuncDef> Parser::parseMainFuncDef() {
    auto mainFuncDef = std::make_unique<MainFuncDef>();
    mainFuncDef->lineno = token_.lineno;

    match(Token::LPARENT);

    match(Token::RPARENT);

    if (is(Token::LBRACE)) {
        mainFuncDef->block = parseBlock();
    } else {
        ErrorReporter::error(token_.lineno, '[Parser] missing Block in MainFuncDef');
    }

    return mainFuncDef;
}

/**
 * @brief 解析`编译单元`
 * @note {Decl} {FuncDef} MainFuncDef
 */
std::unique_ptr<CompUnit> Parser::parseCompUnit() {
    auto compUnit = std::make_unique<CompUnit>();
    compUnit->lineno = token_.lineno;

    while (true) {
        if (is(Token::CONSTTK) || is(Token::STATICTK)) {
            compUnit->var_decls.push_back(parseDecl());
        } else if (is(Token::INTTK, Token::IDENFR)) {
            getToken();
            if (is(Token::IDENFR, Token::LBRACK) || is(Token::IDENFR, Token::EQL)) {
                ungetToken();
                compUnit->var_decls.push_back(parseDecl());
            } else if (is(Token::IDENFR, Token::LPARENT)) {
                if (token_.content == "main") {
                    ungetToken();
                    compUnit->main_func = parseMainFuncDef();
                    return compUnit;
                }
                ungetToken();
                compUnit->func_defs.push_back(parseFuncDef());
            }
        } else if (is(Token::VOIDTK)) {
            compUnit->func_defs.push_back(parseFuncDef());
        } else if (is(Token::EOFTK)) {
            ErrorReporter::error(token_.lineno, '[Parser] can\'t find MainFuncDef');
            return nullptr;
        } else {
            LOG_ERROR(token_.lineno, '[Parser] Unreachable');
            return nullptr;
        }
    }
}