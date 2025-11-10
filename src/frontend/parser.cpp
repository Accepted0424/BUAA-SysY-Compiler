#include "parser.h"

#include "ast.h"
#include "error.h"
#include "logger.h"

void Parser::printNode(std::string node) {
    std::cout << '<' << node << '>' << std::endl;
    if (out_) {
        out_->get() << '<' << node << '>' << std::endl;
    }
}

void Parser::getToken() {
    if (has_unget_) {
        has_unget_ = false;
        cur_ = lookahead_;
        lookahead_ = last_lookahead_;
        return;
    }

    last_ = cur_;
    cur_ = lookahead_;
    lexer_.next(lookahead_);
    last_lookahead_ = lookahead_;

    LOG_DEBUG(last_.lineno, "last_: " + Token::toString(last_));
    LOG_DEBUG(cur_.lineno, "token_: " + Token::toString(cur_));
    LOG_DEBUG(lookahead_.lineno, "lookahead_: " + Token::toString(lookahead_));
}

/**
 * @brief 回退一个token
 * @notes 仅能连续回退一次
 */
void Parser::ungetToken() {
    has_unget_ = true;
    lookahead_ = cur_;
    cur_ = last_;
}

void Parser::skipUntilSemicn() {
    do {
        getToken();
    } while (cur_.type != Token::SEMICN && cur_.type != Token::EOFTK);

    if (cur_.type == Token::EOFTK) {
        return;
    }

    getToken();
}

bool Parser::match(Token::TokenType expected) {
    if (cur_.type == expected) {
        if (out_) {
            std::cout << Token::toString(cur_) << " " << cur_.content << std::endl;
            out_->get() << Token::toString(cur_) << " " << cur_.content << std::endl;
        }
        getToken();
        return true;
    }

    if (expected == Token::SEMICN) {
        ErrorReporter::error(last_.lineno, ERR_MISSING_SEMICOLON);
    } else if (expected == Token::RPARENT) {
        ErrorReporter::error(last_.lineno, ERR_MISSING_RPARENT);
    } else if (expected == Token::RBRACK) {
        ErrorReporter::error(last_.lineno, ERR_MISSING_RBRACK);
    } else {
        ErrorReporter::error(cur_.lineno, "expect '" + Token::toString(expected) + "'");
    }

    return false;
}

bool Parser::is(Token::TokenType type) {
    LOG_DEBUG(cur_.lineno, "Check " + Token::toString(type));
    return cur_.type == type;
}

bool Parser::is(Token::TokenType type, Token::TokenType ahead_type) {
    return cur_.type == type && lookahead_.type == ahead_type;
}

std::unique_ptr<CompUnit> Parser::parse() {
    return parseCompUnit();
}

/**
 * @brief 解析`标识符`
 */
std::unique_ptr<Ident> Parser::parseIdent() {
    auto ident = std::make_unique<Ident>();

    ident->lineno = cur_.lineno;

    ident->content = cur_.content;

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
        // printNode("Decl");
        return std::make_unique<Decl>(std::move(*constDecl));
    }

    if (is(Token::STATICTK) || is(Token::INTTK)) {
        auto varDecl = parseVarDecl();
        // printNode("Decl");
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
    btype->lineno = cur_.lineno;

    if (is(Token::INTTK)) {
        match(Token::INTTK);
        btype->type = Token::toString(Token::INTTK);
        // printNode("Btype");
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
    constDecl->lineno = cur_.lineno;

    match(Token::CONSTTK);

    constDecl->btype = parseBType();

    constDecl->constDefs.push_back(parseConstDef());

    while (is(Token::COMMA)) {
        match(Token::COMMA);
        constDecl->constDefs.push_back(parseConstDef());
    }

    match(Token::SEMICN);

    printNode("ConstDecl");
    return constDecl;
}

/**
 * @brief 解析`常量定义`
 * @note Ident [ '[' ConstExp ']' ] '=' ConstInitVal
 */
std::unique_ptr<ConstDef> Parser::parseConstDef() {
    auto constDef = std::make_unique<ConstDef>();
    constDef->lineno = cur_.lineno;

    constDef->ident = parseIdent();

    if (is(Token::LBRACK)) {
        match(Token::LBRACK);

        constDef->constExp = parseConstExp();

        match(Token::RBRACK);
    }

    match(Token::ASSIGN);

    constDef->constInitVal = parseConstInitVal();

    printNode("ConstDef");
    return constDef;
}

/**
 * @brief 解析`表达式`
 * @note AddExp
 */
std::unique_ptr<Exp> Parser::parseExp() {
    auto exp = std::make_unique<Exp>();
    exp->lineno = cur_.lineno;

    exp->addExp = parseAddExp();

    printNode("Exp");
    return exp;
}

/**
 * @brief 重载 parseExp 以支持传入首个 LVal
 * @note 为了解决 Stmt 解析时赋值语句和表达式语句的二义性。
 */
std::unique_ptr<Exp> Parser::parseExp(std::unique_ptr<LVal> lval) {
    auto exp = std::make_unique<Exp>();
    exp->lineno = cur_.lineno;

    exp->addExp = parseAddExp(std::move(lval));

    printNode("Exp");
    return exp;
}

/**
 * @brief 解析`左值表达式`
 * @note Ident ['[' Exp ']']
 */
std::unique_ptr<LVal> Parser::parseLVal() {
    auto lval = std::make_unique<LVal>();
    lval->lineno = cur_.lineno;

    lval->ident = parseIdent();

    if (is(Token::LBRACK)) {
        match(Token::LBRACK);

        lval->index = parseExp();

        match(Token::RBRACK);
    }

    printNode("LVal");
    return lval;
}

std::unique_ptr<LVal> Parser::parseLValSilent() {
    auto lval = std::make_unique<LVal>();
    lval->lineno = cur_.lineno;

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

    num->lineno = cur_.lineno;

    num->value = cur_.content;

    match(Token::INTCON);

    printNode("Number");
    return num;
}

/**
 * @brief 解析`基本表达式`
 * @note '(' Exp ')' | LVal | Number
 */
std::unique_ptr<PrimaryExp> Parser::parsePrimaryExp() {
    auto primaryExp = std::make_unique<PrimaryExp>();
    primaryExp->lineno = cur_.lineno;

    if (is(Token::LPARENT)) {
        primaryExp->kind = PrimaryExp::EXP;

        match(Token::LPARENT);

        primaryExp->exp = parseExp();

        match(Token::RPARENT);

        printNode("PrimaryExp");
        return primaryExp;
    }

    if (is(Token::IDENFR)) {
        primaryExp->kind = PrimaryExp::LVAL;

        primaryExp->lval = parseLVal();

        printNode("PrimaryExp");
        return primaryExp;
    }

    if (is(Token::INTCON)) {
        primaryExp->kind = PrimaryExp::NUMBER;

        primaryExp->number = parseNumber();

        printNode("PrimaryExp");
        return primaryExp;
    }

    return nullptr;
}

/**
 * @brief 重载 parsePrimaryExp 以支持传入首个 LVal
 * @note 为了解决 Stmt 解析时赋值语句和表达式语句的二义性
 */
std::unique_ptr<PrimaryExp> Parser::parsePrimaryExp(std::unique_ptr<LVal> lval) {
    auto primaryExp = std::make_unique<PrimaryExp>();
    primaryExp->lineno = cur_.lineno;

    if (lval != nullptr) {
        primaryExp->kind = PrimaryExp::LVAL;

        primaryExp->lval = std::move(lval);
        printNode("LVal");

        printNode("PrimaryExp");
        return primaryExp;
    }

    if (is(Token::LPARENT)) {
        primaryExp->kind = PrimaryExp::EXP;

        match(Token::LPARENT);

        primaryExp->exp = parseExp();

        match(Token::RPARENT);

        printNode("PrimaryExp");
        return primaryExp;
    }

    if (is(Token::IDENFR)) {
        primaryExp->kind = PrimaryExp::LVAL;

        match(Token::IDENFR);

        if (lval == nullptr) {
            primaryExp->lval = parseLVal();
        } else {
            primaryExp->lval = std::move(lval);
            printNode("LVal");
        }

        printNode("PrimaryExp");
        return primaryExp;
    }

    if (is(Token::INTCON)) {
        primaryExp->kind = PrimaryExp::NUMBER;

        primaryExp->number = parseNumber();

        printNode("PrimaryExp");
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
    unaryOp->lineno = cur_.lineno;

    if (is(Token::PLUS)) {
        unaryOp->kind = UnaryOp::PLUS;
        match(Token::PLUS);
        printNode("UnaryOp");
        return unaryOp;
    }

    if (is(Token::MINU)) {
        unaryOp->kind = UnaryOp::MINU;
        match(Token::MINU);
        printNode("UnaryOp");
        return unaryOp;
    }

    if (is(Token::NOT)) {
        unaryOp->kind = UnaryOp::NOT;
        match(Token::NOT);
        printNode("UnaryOp");
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
    funcRParams->lineno = cur_.lineno;

    funcRParams->params.push_back(parseExp());

    while (is(Token::COMMA)) {
        match(Token::COMMA);
        funcRParams->params.push_back(parseExp());
    }

    printNode("FuncRParams");
    return funcRParams;
}

/**
 * @brief 解析`一元表达式`
 * @note PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp
 */
std::unique_ptr<UnaryExp> Parser::parseUnaryExp() {
    auto unaryExp = std::make_unique<UnaryExp>();
    unaryExp->lineno = cur_.lineno;

    if (is(Token::IDENFR, Token::LPARENT)) {
        unaryExp->kind = UnaryExp::CALL;
        UnaryExp::Call call;

        call.ident = parseIdent();

        match(Token::LPARENT);

        if (!is(Token::RPARENT)) {
            call.params = parseFuncRParams();
        }

        match(Token::RPARENT);

        unaryExp->call = std::make_unique<UnaryExp::Call>(std::move(call));
        printNode("UnaryExp");
        return unaryExp;
    }

    if (is(Token::LPARENT) || is(Token::IDENFR) || is(Token::INTCON)) {
        unaryExp->kind = UnaryExp::PRIMARY;

        unaryExp->primary = parsePrimaryExp();

        printNode("UnaryExp");
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
            LOG_ERROR(cur_.lineno, "[Parser] invalid UnaryOp in UnaryExp");
        }

        unary.expr = parseUnaryExp();
        unaryExp->unary = std::make_unique<UnaryExp::Unary>(std::move(unary));
        printNode("UnaryExp");
        return unaryExp;
    }

    return nullptr;
}

/**
 * @brief 重载 parseUnaryExp 以支持传入首个 LVal
 * @note 为了解决 Stmt 解析时赋值语句和表达式语句的二义性
 */
std::unique_ptr<UnaryExp> Parser::parseUnaryExp(std::unique_ptr<LVal> lval) {
    auto unaryExp = std::make_unique<UnaryExp>();
    unaryExp->lineno = cur_.lineno;

    if (lval != nullptr) {
        unaryExp->kind = UnaryExp::PRIMARY;

        unaryExp->primary = parsePrimaryExp(std::move(lval));

        printNode("UnaryExp");
        return unaryExp;
    }

    if (is(Token::IDENFR, Token::LPARENT)) {
        unaryExp->kind = UnaryExp::CALL;
        UnaryExp::Call call;

        call.ident = parseIdent();

        match(Token::LPARENT);

        if (!is(Token::RPARENT)) {
            call.params = parseFuncRParams();
        }

        match(Token::RPARENT);

        unaryExp->call = std::make_unique<UnaryExp::Call>(std::move(call));
        printNode("UnaryExp");
        return unaryExp;
    }

    if (is(Token::LPARENT) || is(Token::IDENFR) || is(Token::INTCON)) {
        unaryExp->kind = UnaryExp::PRIMARY;

        unaryExp->primary = parsePrimaryExp(std::move(lval));

        printNode("UnaryExp");
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
            LOG_ERROR(cur_.lineno, "[Parser] invalid UnaryOp in UnaryExp");
        }

        unary.expr = parseUnaryExp();
        unaryExp->unary = std::make_unique<UnaryExp::Unary>(std::move(unary));
        printNode("UnaryExp");
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
    mulExp->lineno = cur_.lineno;

    mulExp->first = parseUnaryExp();

    while (is(Token::MULT) || is(Token::DIV) || is(Token::MOD)) {
        printNode("MulExp");
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

    printNode("MulExp");
    return mulExp;
}

/**
 * @brief 重载 parseMulExp 以支持传入首个 LVal
 * @note 为了解决 Stmt 解析时赋值语句和表达式语句的二义性
 */
std::unique_ptr<MulExp> Parser::parseMulExp(std::unique_ptr<LVal> lval) {
    auto mulExp = std::make_unique<MulExp>();
    mulExp->lineno = cur_.lineno;

    mulExp->first = parseUnaryExp(std::move(lval));

    while (is(Token::MULT) || is(Token::DIV) || is(Token::MOD)) {
        printNode("MulExp");
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

    printNode("MulExp");
    return mulExp;
}

/**
 * @brief 解析`加减表达式`
 * @note 原始文法: MulExp | AddExp ('+' | '−') MulExp
 */
std::unique_ptr<AddExp> Parser::parseAddExp() {
    auto addExp = std::make_unique<AddExp>();
    addExp->lineno = cur_.lineno;

    addExp->first = parseMulExp();

    while (is(Token::PLUS) || is(Token::MINU)) {
        // 除了最后一个MulExp其余应该打印`AddExp`
        printNode("AddExp");

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

    printNode("AddExp");
    return addExp;
}

/**
 * @brief 重载 parseAddExp 以支持传入首个 LVal
 * @note 为了解决 Stmt 解析时赋值语句和表达式语句的二义性
 */
std::unique_ptr<AddExp> Parser::parseAddExp(std::unique_ptr<LVal> lval) {
    auto addExp = std::make_unique<AddExp>();
    addExp->lineno = cur_.lineno;

    addExp->first = parseMulExp(std::move(lval));

    while (is(Token::PLUS) || is(Token::MINU)) {
        // 除了最后一个MulExp其余应该打印`AddExp`
        printNode("AddExp");

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

    printNode("AddExp");
    return addExp;
}

/**
 * @brief 解析`常量表达式`
 * @note AddExp
 */
std::unique_ptr<ConstExp> Parser::parseConstExp() {
    auto constExp = std::make_unique<ConstExp>();
    constExp->lineno = cur_.lineno;

    constExp->addExp = parseAddExp();

    printNode("ConstExp");
    return constExp;
}

/**
 * @brief 解析`常量初值`
 * @note ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}'
 */
std::unique_ptr<ConstInitVal> Parser::parseConstInitVal() {
    auto constInitVal = std::make_unique<ConstInitVal>();
    constInitVal->lineno = cur_.lineno;

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

    printNode("ConstInitVal");
    return constInitVal;
}

/**
 * @brief 解析`变量初值`
 * @note Exp | '{' [ Exp { ',' Exp } ] '}'
 */
std::unique_ptr<InitVal> Parser::parseInitVal() {
    auto initVal = std::make_unique<InitVal>();
    initVal->lineno = cur_.lineno;

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

    printNode("InitVal");
    return initVal;
}

/**
 * @brief 解析`变量定义`
 * @note Ident [ '[' ConstExp ']' ] | Ident [ '[' ConstExp ']' ] '=' InitVal
 */
std::unique_ptr<VarDef> Parser::parseVarDef() {
    auto varDef = std::make_unique<VarDef>();
    varDef->lineno = cur_.lineno;

    varDef->ident = parseIdent();

    if (is(Token::LBRACK)) {
        match(Token::LBRACK);

        varDef->constExp = parseConstExp();

        match(Token::RBRACK);
    }

    if (is(Token::ASSIGN)) {
        match(Token::ASSIGN);

        varDef->initVal = parseInitVal();
    }

    printNode("VarDef");
    return varDef;
}

/**
 * @brief 解析`变量声明`
 * @note [ 'static' ] BType VarDef { ',' VarDef } ';'
 */
std::unique_ptr<VarDecl> Parser::parseVarDecl() {
    auto varDecl = std::make_unique<VarDecl>();
    varDecl->lineno = cur_.lineno;

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

    printNode("VarDecl");
    return varDecl;
}

/**
 * @brief 解析`for语句`
 * @note LVal '=' Exp { ',' LVal '=' Exp }
 */
std::unique_ptr<ForStmt> Parser::parseForStmt() {
    auto forStmt = std::make_unique<ForStmt>();
    forStmt->lineno = cur_.lineno;

    auto lValFirst = parseLVal();
    match(Token::ASSIGN);
    auto expFirst = parseExp();
    forStmt->assigns.push_back({std::move(lValFirst), std::move(expFirst)});

    while (is(Token::COMMA)) {
        match(Token::COMMA);
        auto lVal = parseLVal();
        match(Token::ASSIGN);
        auto exp = parseExp();
        forStmt->assigns.push_back({std::move(lVal), std::move(exp)});
    }

    printNode("ForStmt");
    return forStmt;
}

/**
 * @brief 解析`关系表达式`
 * @note AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp
 */
std::unique_ptr<RelExp> Parser::parseRelExp() {
    auto relExp = std::make_unique<RelExp>();
    relExp->lineno = cur_.lineno;

    relExp->addExpFirst = parseAddExp();

    while (is(Token::LEQ) || is(Token::GEQ) ||
        is(Token::LSS) || is(Token::GRE)) {
        printNode("RelExp");
        RelExp::Operator op;
        if (is(Token::LEQ)) {
            match(Token::LEQ);
            op = RelExp::LEQ;
        } else if (is(Token::GEQ)) {
            match(Token::GEQ);
            op = RelExp::GEQ;
        } else if (is(Token::LSS)) {
            match(Token::LSS);
            op = RelExp::LSS;
        } else {
            match(Token::GRE);
            op = RelExp::GRE;
        }
        auto addExp = parseAddExp();
        relExp->addExpRest.push_back({op, std::move(addExp)});
    }

    printNode("RelExp");
    return relExp;
}

/**
 * @brief 解析`相等性表达式`
 * @note RelExp | EqExp ('==' | '!=') RelExp
 */
std::unique_ptr<EqExp> Parser::parseEqExp() {
    auto eqExp = std::make_unique<EqExp>();
    eqExp->lineno = cur_.lineno;

    eqExp->relExpFirst = parseRelExp();

    while (is(Token::EQL) || is(Token::NEQ)) {
        printNode("EqExp");
        EqExp::Operator op;
        if (is(Token::EQL)) {
            match(Token::EQL);
            op = EqExp::EQL;
        } else {
            match(Token::NEQ);
            op = EqExp::NEQ;
        }
        auto relExp = parseRelExp();
        eqExp->relExpRest.push_back({op, std::move(relExp)});
    }

    printNode("EqExp");
    return eqExp;
}

/**
 * @brief 解析`逻辑与表达式`
 * @note EqExp | LAndExp '&&' EqExp
 */
std::unique_ptr<LAndExp> Parser::parseLAndExp() {
    auto lAndExp = std::make_unique<LAndExp>();
    lAndExp->lineno = cur_.lineno;

    lAndExp->eqExps.push_back(parseEqExp());

    while (is(Token::AND)) {
        printNode("LAndExp");
        match(Token::AND);
        lAndExp->eqExps.push_back(parseEqExp());
    }

    printNode("LAndExp");
    return lAndExp;
}

/**
 * @brief 解析`逻辑或表达式`
 * @note LAndExp | LOrExp '||' LAndExp
 */
std::unique_ptr<LOrExp> Parser::parseLOrExp() {
    auto lOrExp = std::make_unique<LOrExp>();
    lOrExp->lineno = cur_.lineno;

    lOrExp->eqExps.push_back(parseLAndExp());

    while (is(Token::OR)) {
        printNode("LOrExp");
        match(Token::OR);
        lOrExp->eqExps.push_back(parseLAndExp());
    }

    printNode("LOrExp");
    return lOrExp;
}

/**
 * @brief 解析`条件表达式`
 * @note LOrExp
 */
std::unique_ptr<Cond> Parser::parseCond() {
    auto cond = std::make_unique<Cond>();
    cond->lineno = cur_.lineno;

    cond->lOrExp = parseLOrExp();

    printNode("Cond");
    return cond;
}

/**
 * @brief 解析`语句`
 * @note
 * - LVal '=' Exp ';'
 * - [Exp] ';'
 * - Block
 * - 'if' '(' Cond ')' Stmt [ 'else' Stmt ]
 * - 'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt
 * - 'break' ';'
 * - 'continue' ';'
 * - 'return' [Exp] ';'
 * - 'printf''('StringConst {','Exp}')' ';'
 */
std::unique_ptr<Stmt> Parser::parseStmt() {
    auto stmt = std::make_unique<Stmt>();
    stmt->lineno = cur_.lineno;

    std::unique_ptr<LVal> lVal;

    if (is(Token::SEMICN)) {
        match(Token::SEMICN);
        stmt->kind = Stmt::EXP;
        stmt->exp = nullptr;
        printNode("Stmt");
        return stmt;
    }

    // 这里需要注意：赋值语句和表达式语句第一个语法成分都有可能是 LVal
    if (is(Token::IDENFR)) {
        if (is(Token::IDENFR) && !is(Token::IDENFR, Token::LPARENT)) {
            // 此时可以确定是 LVal，但无法确定是赋值语句还是表达式语句，先解析 LVal，后续都能用到
            lVal = parseLValSilent();
        }

        if (is(Token::ASSIGN)) {
            printNode("LVal");
            match(Token::ASSIGN);
            stmt->kind = Stmt::ASSIGN;
            stmt->assignStmt.lVal = std::move(lVal);
            stmt->assignStmt.exp = parseExp();
            match(Token::SEMICN);
            printNode("Stmt");
            return stmt;
        }

        stmt->kind = Stmt::EXP;
        if (lVal == nullptr) {
            stmt->exp = parseExp();
        } else {
            stmt->exp = parseExp(std::move(lVal));
        }
        match(Token::SEMICN);
        printNode("Stmt");
        return stmt;
    }

    if (is(Token::PLUS) || is(Token::MINU) || is(Token::NOT) ||
        is(Token::LPARENT) || is(Token::INTCON)) {
        stmt->kind = Stmt::EXP;
        stmt->exp = parseExp();
        match(Token::SEMICN);
        printNode("Stmt");
        return stmt;
    }

    if (is(Token::LBRACE)) {
        stmt->kind = Stmt::BLOCK;
        stmt->block = parseBlock();
        printNode("Stmt");
        return stmt;
    }

    if (is(Token::IFTK)) {
        match(Token::IFTK);
        stmt->kind = Stmt::IF;
        match(Token::LPARENT);
        stmt->ifStmt.cond = parseCond();
        match(Token::RPARENT);
        stmt->ifStmt.thenStmt = parseStmt();
        if (is(Token::ELSETK)) {
            match(Token::ELSETK);
            stmt->ifStmt.elseStmt = parseStmt();
        }
        printNode("Stmt");
        return stmt;
    }

    if (is(Token::FORTK)) {
        match(Token::FORTK);
        stmt->kind = Stmt::FOR;
        match(Token::LPARENT);
        if (!is(Token::SEMICN)) {
            stmt->forStmt.forStmtFirst = parseForStmt();
        }
        match(Token::SEMICN);
        if (!is(Token::SEMICN)) {
            stmt->forStmt.cond = parseCond();
        }
        match(Token::SEMICN);
        if (!is(Token::RPARENT)) {
            stmt->forStmt.forStmtSecond = parseForStmt();
        }
        match(Token::RPARENT);
        stmt->forStmt.stmt = parseStmt();
        printNode("Stmt");
        return stmt;
    }

    if (is(Token::BREAKTK)) {
        match(Token::BREAKTK);
        stmt->kind = Stmt::BREAK;
        match(Token::SEMICN);
        printNode("Stmt");
        return stmt;
    }

    if (is(Token::CONTINUETK)) {
        match(Token::CONTINUETK);
        stmt->kind = Stmt::CONTINUE;
        match(Token::SEMICN);
        printNode("Stmt");
        return stmt;
    }

    if (is(Token::RETURNTK)) {
        match(Token::RETURNTK);
        stmt->kind = Stmt::RETURN;
        if (!is(Token::SEMICN)) {
            stmt->returnExp = parseExp();
        }
        match(Token::SEMICN);
        printNode("Stmt");
        return stmt;
    }

    if (is(Token::PRINTFTK)) {
        match(Token::PRINTFTK);
        match(Token::LPARENT);
        stmt->kind = Stmt::PRINTF;
        if (is(Token::STRCON)) {
            stmt->printfStmt.str = cur_.content;
            match(Token::STRCON);
        } else {
            ErrorReporter::error(cur_.lineno, "[Parser] missing StringConst in Printf");
        }
        while (is(Token::COMMA)) {
            match(Token::COMMA);
            stmt->printfStmt.args.push_back(parseExp());
        }
        match(Token::RPARENT);
        match(Token::SEMICN);

        printNode("Stmt");
        return stmt;
    }

    return nullptr;
}

/**
 * @brief 解析`语句块项`
 * @note Decl | Stmt
 */
std::unique_ptr<BlockItem> Parser::parseBlockItem() {
    auto blockItem = std::make_unique<BlockItem>();
    blockItem->lineno = cur_.lineno;

    if (is(Token::CONSTTK) || is(Token::STATICTK) || is(Token::INTTK)) {
        blockItem->kind = BlockItem::DECL;
        blockItem->decl = parseDecl();
        // printNode("BlockItem");
        return blockItem;
    }

    blockItem->kind = BlockItem::STMT;
    blockItem->stmt = parseStmt();

    // printNode("BlockItem");
    return blockItem;
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

    block->lineno = cur_.lineno;

    match(Token::RBRACE);

    printNode("Block");
    return block;
}

/**
 * @brief 解析`主函数定义`
 * @note 'int' 'main' '(' ')' Block
 */
std::unique_ptr<MainFuncDef> Parser::parseMainFuncDef() {
    auto mainFuncDef = std::make_unique<MainFuncDef>();
    mainFuncDef->lineno = cur_.lineno;

    match(Token::INTTK);

    match(Token::MAINTK);

    match(Token::LPARENT);

    match(Token::RPARENT);

    if (is(Token::LBRACE)) {
        mainFuncDef->block = parseBlock();
    } else {
        ErrorReporter::error(cur_.lineno, "[Parser] missing Block in MainFuncDef");
    }

    printNode("MainFuncDef");
    return mainFuncDef;
}

std::unique_ptr<FuncType> Parser::parseFuncType() {
    auto funcType = std::make_unique<FuncType>();
    funcType->lineno = cur_.lineno;

    if (is(Token::INTTK)) {
        match(Token::INTTK);
        funcType->kind = FuncType::INT;
        printNode("FuncType");
        return funcType;
    }

    if (is(Token::VOIDTK)) {
        match(Token::VOIDTK);
        funcType->kind = FuncType::VOID;
        printNode("FuncType");
        return funcType;
    }

    return nullptr;
}

/**
 * @brief 解析`函数形参`
 * @note BType Ident ['[' ']']
 */
std::unique_ptr<FuncFParam> Parser::parseFuncFParam() {
    auto funcFParam = std::make_unique<FuncFParam>();
    funcFParam->lineno = cur_.lineno;

    funcFParam->btype = parseBType();

    funcFParam->ident = parseIdent();

    if (is(Token::LBRACK)) {
        match(Token::LBRACK);

        funcFParam->isArray = true;

        match(Token::RBRACK);
    }

    printNode("FuncFParam");
    return funcFParam;
}

/**
 * @brief 解析`函数形参表`
 * @note FuncFParam { ',' FuncFParam }
 */
std::unique_ptr<FuncFParams> Parser::parseFuncFParams() {
    auto funcFParams = std::make_unique<FuncFParams>();
    funcFParams->lineno = cur_.lineno;

    funcFParams->params.push_back(parseFuncFParam());

    while (is(Token::COMMA)) {
        match(Token::COMMA);
        funcFParams->params.push_back(parseFuncFParam());
    }

    printNode("FuncFParams");
    return funcFParams;
}

/**
 * @brief 解析`函数定义`
 * @note FuncType Ident '(' [FuncFParams] ')' Block
 */
std::unique_ptr<FuncDef> Parser::parseFuncDef() {
    auto funcDef = std::make_unique<FuncDef>();
    funcDef->lineno = cur_.lineno;

    if (is(Token::INTTK) || is(Token::VOIDTK)) {
        funcDef->funcType = parseFuncType();
    }

    funcDef->ident = parseIdent();

    match(Token::LPARENT);

    if (!is(Token::RPARENT)) {
        funcDef->funcFParams = parseFuncFParams();
        match(Token::RPARENT);
    } else {
        match(Token::RPARENT);
    }

    funcDef->block = parseBlock();

    printNode("FuncDef");
    return funcDef;
}

/**
 * @brief 解析`编译单元`
 * @note {Decl} {FuncDef} MainFuncDef
 */
std::unique_ptr<CompUnit> Parser::parseCompUnit() {
    auto compUnit = std::make_unique<CompUnit>();
    compUnit->lineno = cur_.lineno;

    getToken();
    while (true) {
        if (is(Token::CONSTTK) || is(Token::STATICTK)) {
            compUnit->decls.push_back(parseDecl());
        } else if (is(Token::INTTK, Token::IDENFR)) {
            getToken();
            if (is(Token::IDENFR, Token::LPARENT)) {
                ungetToken();
                compUnit->func_defs.push_back(parseFuncDef());
            } else {
                ungetToken();
                compUnit->decls.push_back(parseDecl());
            }
        } else if (is(Token::INTTK, Token::MAINTK)) {
            compUnit->main_func = parseMainFuncDef();
            printNode("CompUnit");
            return compUnit;
        } else if (is(Token::VOIDTK)) {
            compUnit->func_defs.push_back(parseFuncDef());
        } else if (is(Token::EOFTK)) {
            ErrorReporter::error(cur_.lineno, "[Parser] can\'t find MainFuncDef");
            return nullptr;
        } else {
            LOG_ERROR(cur_.lineno, "[Parser] Unreachable");
            return nullptr;
        }
    }
}