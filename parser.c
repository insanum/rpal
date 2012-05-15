
/*
 * RPAL Abstract Syntax Tree (AST) generator.
 *
 * Author: Eric Davis (edavis@insanum.com)
 *
 * License: (Beerware) This code is public domain and can be used without
 * restriction.  Just buy me a beer if we should ever meet.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>


#define IS_OPERATOR_SYMBOL(c)                                        \
    (                                                                \
     ((c) == '+') || ((c) == '-') || ((c) == '*') || ((c) == '<') || \
     ((c) == '>') || ((c) == '&') || ((c) == '.') || ((c) == '@') || \
     ((c) == '/') || ((c) == ':') || ((c) == '=') || ((c) == '~') || \
     ((c) == '|') || ((c) == '$') || ((c) == '!') || ((c) == '#') || \
     ((c) == '%') || ((c) == '^') || ((c) == '_') || ((c) == '[') || \
     ((c) == ']') || ((c) == '{') || ((c) == '}') || ((c) == '"') || \
     ((c) == '`') || ((c) == '?')                                    \
    )

#define IS_DIGIT(c)               \
    (                             \
     ((c) >= '0') && ((c) <= '9') \
    )

#define IS_LETTER(c)                                                  \
    (                                                                 \
     (((c) >= 'A') && ((c) <= 'Z')) || (((c) >= 'a') && ((c) <= 'z')) \
    )

#define IS_PUNCTION(c)                                            \
    (                                                             \
     ((c) == '(') || ((c) == ')') || ((c) == ';') || ((c) == ',') \
    )

#define IS_SPACE(c)                                                  \
    (                                                                \
     ((c) == ' ') || ((c) == '\t') || ((c) == '\n') || ((c) == '\r') \
    )

#define IS_IDENTIFIER_CHAR(c)                    \
    (                                            \
     IS_LETTER(c) || IS_DIGIT(c) || ((c) == '_') \
    )

#define IS_STRING_CHAR(c)                                    \
    (                                                        \
     IS_LETTER(c) || IS_DIGIT(c) || IS_OPERATOR_SYMBOL(c) || \
     ((c) == '(') || ((c) == ')') || ((c) == ';') ||         \
     ((c) == ',') || ((c) == ' ')                            \
    )

#define IS_KEYWORD(s)                                               \
    (                                                               \
     (strcmp((s), "let")   == 0) || (strcmp((s), "in")     == 0) || \
     (strcmp((s), "fn")    == 0) || (strcmp((s), "where")  == 0) || \
     (strcmp((s), "aug")   == 0) || (strcmp((s), "or")     == 0) || \
     (strcmp((s), "not")   == 0) || (strcmp((s), "gr")     == 0) || \
     (strcmp((s), "ge")    == 0) || (strcmp((s), "ls")     == 0) || \
     (strcmp((s), "le")    == 0) || (strcmp((s), "eq")     == 0) || \
     (strcmp((s), "ne")    == 0) || (strcmp((s), "true")   == 0) || \
     (strcmp((s), "false") == 0) || (strcmp((s), "nil")    == 0) || \
     (strcmp((s), "dummy") == 0) || (strcmp((s), "within") == 0) || \
     (strcmp((s), "and")   == 0) || (strcmp((s), "rec")    == 0) || \
     (strcmp((s), "rec")   == 0)                                    \
    )

typedef enum
{
    T_KEYWORD,
    T_IDENTIFIER,
    T_INTEGER,
    T_OPERATOR,
    T_STRING,
    T_PUNCTION
} TokenType;

typedef struct _token
{
    TAILQ_ENTRY(_token)         siblings;
    TokenType                   type;
    int                         length; /* pStr length w/ delim */
    char *                      pStr;
    TAILQ_HEAD(subtree, _token) children;
} Token;

TAILQ_HEAD(tailhead, _token) thead;

#define T_MATCH(t, tt, s) \
    ((t) && (s) && ((t)->type == tt) && strcmp((t)->pStr, (s)) == 0)

#define T_NEXT(t)                 ((Token *)(t)->siblings.tqe_next)
#define T_PREV(t)                 ((Token *)(t)->siblings.tqe_prev)

#define T_FIRST()                 ((Token *)thead.tqh_first)
#define T_SECOND()                T_NEXT(T_FIRST())
#define T_LAST()                  ((Token *)thead.tqh_last)
#define T_INSERT_HEAD(t)          TAILQ_INSERT_HEAD(&thead, (t), siblings)
#define T_INSERT_TAIL(t)          TAILQ_INSERT_TAIL(&thead, (t), siblings)
#define T_REMOVE(t)               TAILQ_REMOVE(&thead, (t), siblings)

#define T_FIRST_CHILD(t)          ((Token *)(t)->children.tqh_first)
#define T_SECOND_CHILD(t)         T_NEXT(T_FIRST_CHILD(t))
#define T_LAST_CHILD(t)           ((Token *)(t)->children.tqh_last)
#define T_INSERT_HEAD_CHILD(t, c) TAILQ_INSERT_HEAD(&(t)->children, (c), siblings)
#define T_INSERT_TAIL_CHILD(t, c) TAILQ_INSERT_TAIL(&(t)->children, (c), siblings)
#define T_REMOVE_CHILD(t, c)      TAILQ_REMOVE(&(t)->children, (c), siblings)

#define T_PUSH(t)                 T_INSERT_HEAD(t)

inline Token * T_POP(void)
{
    Token * pToken = T_FIRST();
    T_REMOVE(pToken);
    return pToken;
}

inline Token * T_POP_OP(void) /* T_POP plus type change to T_OPERATOR */
{
    Token * pToken = T_POP();
    pToken->type = T_OPERATOR;
    return pToken;
}

#define T_POP_DUMP() TokenFree(T_POP())

inline void T_VERIFY(TokenType type, const char * pStr)
{
    if (!T_MATCH(T_FIRST(), type, pStr))
    {
        printf("ERROR: syntax error at token ('%s'), expected ('%s')\n",
               T_FIRST()->pStr, pStr);
        exit(1);
    }
}

/* forward declarations */
void Parser_E(void);
void Parser_D(void);
extern int optind;
void DumpAST(Token * pRoot, int indent);

#define LOG_RULE_OFF 0x0
#define LOG_RULE_TDN 0x1
#define LOG_RULE_BUP 0x2
int log_rules = LOG_RULE_OFF;
#define LOG_TDN(s, ...) if (log_rules & LOG_RULE_TDN) printf("TDN: " s "\n", ## __VA_ARGS__);
#define LOG_BUP(s, ...) if (log_rules & LOG_RULE_BUP) printf("BUP: " s "\n", ## __VA_ARGS__);


char tokenstr[256]; /* be careful, global string storage for printf */

char * TokenToStr(Token * pToken)
{
    switch (pToken->type)
    {
    case T_KEYWORD:

        snprintf(tokenstr, sizeof(tokenstr), "%s", pToken->pStr);
        return tokenstr;

    case T_IDENTIFIER:

        snprintf(tokenstr, sizeof(tokenstr), "<ID:%s>", pToken->pStr);
        return tokenstr;

    case T_INTEGER:

        snprintf(tokenstr, sizeof(tokenstr), "<INT:%s>", pToken->pStr);
        return tokenstr;

    case T_OPERATOR:

        /* XXX "()" hack to match RPAL interpreter AST output */
        snprintf(tokenstr, sizeof(tokenstr), "%s",
                 (strcmp(pToken->pStr, "()") == 0) ? "<()>" : pToken->pStr);
        return tokenstr;

    case T_STRING:

        snprintf(tokenstr, sizeof(tokenstr), "<STR:'%s'>", pToken->pStr);
        return tokenstr;

    case T_PUNCTION:

        snprintf(tokenstr, sizeof(tokenstr), "%s", pToken->pStr);
        return tokenstr;

    default:

        strcpy(tokenstr, "<unknown>");
        return tokenstr;
    }
}


/*
 * c  = 0 && pStr  = NULL  => init with zero length string
 * c != 0 && pStr  = NULL  => init with c
 * c  = 0 && pStr != NULL  => init with pStr
 * c != 0 && pStr != NULL  => invalid
 */ 
Token * TokenAlloc(TokenType type, char c, const char * pStr)
{
    Token * pToken;
    int len;

    if ((pToken = (Token *)malloc(sizeof(Token))) == NULL)
    {
        perror("Failed to malloc memory");
        exit(1);
    }

    memset(pToken, 0, sizeof(Token));

    len = (((c == 0) && (pStr == NULL)) ? 1                  :
           ((c != 0) && (pStr == NULL)) ? 2                  :
           ((c == 0) && (pStr != NULL)) ? (strlen(pStr) + 1) : 0);

    if (!len)
    {
        printf("ERROR: invalid token string length\n");
        exit(1);
    }

    if ((pToken->pStr = (char *)malloc(sizeof(char) * len)) == NULL)
    {
        perror("Failed to malloc memory");
        exit(1);
    }

    pToken->type   = type;
    pToken->length = len;
    TAILQ_INIT(&pToken->children);

    if ((c == 0) && (pStr == NULL))
    {
        pToken->pStr[0] = 0;
    }
    else if ((c != 0) && (pStr == NULL))
    {
        pToken->pStr[0] = c;
        pToken->pStr[1] = 0;
    }
    else /* ((c == 0) && (pStr != NULL)) */
    {
        strcpy(pToken->pStr, pStr);
    }

    return pToken;
}


/* Free a Token. */
void TokenFree(Token * pToken)
{
    free(pToken->pStr);
    free(pToken);
}


/*
 * Add a new character to the end of a Token's string.
 *
 * Note that this function uses realloc() to generate a new copied string
 * buffer with the character tacked on the end.  This is not efficient when
 * continuously adding one character at a time... but is tolerable for this
 * project.
 */
void TokenAddChar(Token * pToken, char c)
{
    pToken->length++;

    if ((pToken->pStr = (char *)realloc(pToken->pStr, pToken->length)) == NULL)
    {
        perror("Failed to realloc memory");
        exit(1);
    }

    pToken->pStr[pToken->length - 2] = c;
    pToken->pStr[pToken->length - 1] = 0;
}


/* Get the next character in the file. */
char CharGet(int fd)
{
    char c;
    int rc;

    if ((rc = read(fd, &c, 1)) == -1)
    {
        perror("Failed to read file (CharGet)");
        exit(1);
    }

    return (rc == 0) ? 0 : c;
}


/* Get the next character in the file without seeking the read pointer. */
char CharPeekNext(int fd)
{
    char c;
    int rc;

    if ((rc = read(fd, &c, 1)) == -1)
    {
        perror("Failed to read file (CharPeekNext)");
        exit(1);
    }

    if (lseek(fd, -1, SEEK_CUR) == -1)
    {
        perror("Failed to lseek file (CharPeekNext)");
        exit(1);
    }

    return (rc == 0) ? 0 : c;
}


/* Skip over the next character in the file. */
void CharSkipNext(int fd)
{
    char c;

    if (read(fd, &c, 1) == -1)
    {
        perror("Failed to read file (CharSkipNext)");
        exit(1);
    }
}


/* Scan (skip over) an RPAL comment. */
void Scanner_Comment(int fd)
{
    char c;

    /*
     * The RPAL lexicon specifies the set of characters allowed in a comment
     * but instead we shortcut that here and just wipe out everything up to
     * the end of the line.
     */
    while ((c = CharGet(fd)) != 0)
    {
        if (c == '\n') break;
    }
}


/* Scan and tokenize an RPAL string. */
void Scanner_String(int fd)
{
    Token * pToken = TokenAlloc(T_STRING, 0, NULL);
    char c;

    while ((c = CharGet(fd)) != 0)
    {
        if (c == '\'') /* end of the string */
        {
            break;
        }
        else if (c == '\\') /* string escape sequence */
        {
            c = CharPeekNext(fd);

            if ((c == 't') || (c == 'n') || (c == '\\') || (c == '\''))
            {
                CharSkipNext(fd);
                TokenAddChar(pToken, '\\');
                TokenAddChar(pToken, c);
            }
            else
            {
                printf("ERROR: invalid string escape sequence (\\%c)\n", c);
                exit(1);
            }
        }
        else if (IS_STRING_CHAR(c)) /* valid string char */
        {
            TokenAddChar(pToken, c);
            continue;
        }
        else
        {
            printf("ERROR: invalid string character (%c)", c);
            exit(1);
        }
    }

    T_INSERT_TAIL(pToken);
}


/* Scan and tokenize an RPAL operator. */
void Scanner_Operator(int fd, char c)
{
    Token * pToken = TokenAlloc(T_OPERATOR, c, NULL);

    while ((c = CharPeekNext(fd)) != 0)
    {
        if (IS_OPERATOR_SYMBOL(c))
        {
            TokenAddChar(pToken, c);
            CharSkipNext(fd);
            continue;
        }

        break;
    }

    T_INSERT_TAIL(pToken);
}


/* Scan and tokenize an RPAL integer. */
void Scanner_Integer(int fd, char c)
{
    Token * pToken = TokenAlloc(T_INTEGER, c, NULL);

    while ((c = CharPeekNext(fd)) != 0)
    {
        if (IS_DIGIT(c))
        {
            TokenAddChar(pToken, c);
            CharSkipNext(fd);
            continue;
        }

        break;
    }

    T_INSERT_TAIL(pToken);
}


/* Scan and tokenize an RPAL identifier/keyword. */
void Scanner_Identifier(int fd, char c)
{
    Token * pToken = TokenAlloc(T_IDENTIFIER, c, NULL);

    while ((c = CharPeekNext(fd)) != 0)
    {
        if (IS_IDENTIFIER_CHAR(c))
        {
            TokenAddChar(pToken, c);
            CharSkipNext(fd);
            continue;
        }

        break;
    }

    if (IS_KEYWORD(pToken->pStr))
    {
        pToken->type = T_KEYWORD;
    }

    T_INSERT_TAIL(pToken);
}


/* Scan and tokenize an RPAL punction. */
void Scanner_Punction(int fd, char c)
{
    Token * pToken = TokenAlloc(T_PUNCTION, c, NULL);
    T_INSERT_TAIL(pToken);
}


/* Scan an RPAL program! */
void Scanner(int fd)
{
    char c;

    while ((c = CharGet(fd)) != 0)
    {
        if (IS_SPACE(c)) /* skip open whitespace */
        {
            continue;
        }
        else if ((c == '/') && (CharPeekNext(fd) == '/')) /* skip comments */
        {
            Scanner_Comment(fd);
        }
        else if (c == '\'') /* grab the string */
        {
            Scanner_String(fd);
        }
        else if (IS_OPERATOR_SYMBOL(c)) /* grab the operator */
        {
            Scanner_Operator(fd, c);
        }
        else if (IS_DIGIT(c)) /* grab the integer */
        {
            Scanner_Integer(fd, c);
        }
        else if (IS_LETTER(c)) /* grab the identifier/keyword */
        {
            Scanner_Identifier(fd, c);
        }
        else if (IS_PUNCTION(c)) /* grab the punction */
        {
            Scanner_Punction(fd, c);
        }
        else /* Doh! */
        {
            printf("ERROR: unable to process char (%c)\n", c);
            exit(1);
        }
    }
}


/*
 * Vl -> '<IDENTIFIER>' list ','   => ','?
 */
void Parser_Vl(void)
{
    Token * pID;
    Token * pOp;

    /*
     * The equivalent rule is:
     *
     *   Vl -> '<IDENTIFIER>' ( ',' '<IDENTIFIER>' )+   => ','
     *      -> '<IDENTIFIER>'
     *
     * If there aren't any trailing ID tokens the resulting tree is just the
     * first ID.  If there are multiple IDs then the tree is rooted with a ','
     * token.
     */

    if (T_FIRST()->type != T_IDENTIFIER)
    {
        printf("ERROR: syntax error at token ('%s'), expected ID\n",
               T_FIRST()->pStr);
        exit(1);
    }

    LOG_TDN("Vl -> '<IDENTIFIER>' list ','");

    if (T_MATCH(T_SECOND(), T_PUNCTION, ","))
    {
        pOp = TokenAlloc(T_OPERATOR, 0, ","); /* create a ',' token */

        while (T_MATCH(T_SECOND(), T_PUNCTION, ","))
        {
            pID = T_POP(); /* pop ID */
            T_INSERT_TAIL_CHILD(pOp, pID); /* child ID */ 

            T_POP_DUMP(); /* dump the ',' */
        }

        pID = T_POP(); /* pop ID */
        T_INSERT_TAIL_CHILD(pOp, pID); /* child ID */ 

        T_PUSH(pOp); /* push tree Op */
    }

    LOG_BUP("Vl -> '<IDENTIFIER>' list ','");
}



/*
 * Vb -> '<IDENTIFIER>'
 *    -> '(' Vl ')'
 *    -> '(' ')'          => '()'
 */
void Parser_Vb(void)
{
    Token * pVl;
    Token * pOp;

    if (T_FIRST()->type == T_IDENTIFIER)
    {
        /* nothing to do, leave the ID token on the stack */
        LOG_TDN("Vb -> '<IDENTIFIER>'");
        LOG_BUP("Vb -> '<IDENTIFIER>'");
    }
    else if (T_MATCH(T_FIRST(), T_PUNCTION, "(") &&
             T_MATCH(T_SECOND(), T_PUNCTION, ")"))
    {
        LOG_TDN("Vb -> '(' ')'");

        T_POP_DUMP(); /* dump the '(' */
        T_POP_DUMP(); /* dump the ')' */

        pOp = TokenAlloc(T_OPERATOR, 0, "()"); /* create a '()' token */

        T_PUSH(pOp); /* push tree Op */

        LOG_BUP("Vb -> '(' ')'");
    }
    else if (T_MATCH(T_FIRST(), T_PUNCTION, "("))
    {
        LOG_TDN("Vb -> '(' Vl ')'");

        T_POP_DUMP(); /* dump the '(' */

        Parser_Vl();

        pVl = T_POP(); /* pop Vl */

        T_VERIFY(T_PUNCTION, ")");
        T_POP_DUMP(); /* dump the ')' */

        T_PUSH(pVl); /* push Vl back on the stack */

        LOG_BUP("Vb -> '(' Vl ')'");
    }
    else
    {
        printf("ERROR: syntax error at token ('%s'), expected ('(')\n",
               T_FIRST()->pStr);
        exit(1);
    }
}


/*
 * Db -> Vl '=' E                   => '='
 *    -> '<IDENTIFIER>' Vb+ '=' E   => 'fcn_form'
 *    -> '(' D ')'
 */
void Parser_Db(void)
{
    Token * pD;
    Token * pID;
    Token * pVl;
    Token * pVb;
    Token * pOp;
    Token * pE;

    if (T_MATCH(T_FIRST(), T_PUNCTION, "("))
    {
        LOG_TDN("Db -> '(' D ')'");

        T_POP_DUMP(); /* dump the '(' */

        Parser_D();

        pD = T_POP(); /* pop D */

        T_VERIFY(T_PUNCTION, ")");
        T_POP_DUMP(); /* dump the ')' */

        T_PUSH(pD); /* push D back on the stack */

        LOG_BUP("Db -> '(' D ')'");
        return;
    }

    /*
     * The last two rules are a bit tricky since both start with an ID.  After
     * expanding the rules further it's easy to see how they differ:
     *
     * Db -> Vl '=' E
     *
     *    => '<IDENTIFIER>' ( ',' '<IDENTIFIER>' )+ '=' E
     *    => '<IDENTIFIER>'                         '=' E
     *
     * Db -> '<IDENTIFIER>' Vb+ '=' E
     *
     *    => '<IDENTIFIER>' '<IDENTIFIER>' '=' E
     *    => '<IDENTIFIER>' '(' Vl ')'     '=' E
     *    => '<IDENTIFIER>' '(' ')'        '=' E
     */

    if (T_FIRST()->type != T_IDENTIFIER)
    {
        printf("ERROR: syntax error at token ('%s'), expected ID\n",
               T_FIRST()->pStr);
        exit(1);
    }

    if (T_MATCH(T_SECOND(), T_PUNCTION, ",") ||
        T_MATCH(T_SECOND(), T_OPERATOR, "="))
    {
        LOG_TDN("Db -> Vl '=' E");

        Parser_Vl();

        pVl = T_POP(); /* pop Vl */

        T_VERIFY(T_OPERATOR, "=");
        pOp = T_POP_OP(); /* pop '=' */

        Parser_E();

        pE = T_POP(); /* pop E */

        T_INSERT_TAIL_CHILD(pOp, pVl); /* left child Vl */
        T_INSERT_TAIL_CHILD(pOp, pE);  /* right child E */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Db -> Vl '=' E");
    }
    else if ((T_SECOND()->type == T_IDENTIFIER) ||
             T_MATCH(T_SECOND(), T_PUNCTION, "("))
    {
        LOG_TDN("Db -> '<IDENTIFIER>' Vb+ '=' E");

        /* XXX using "function_form" to match RPAL interpreter AST output */
        pOp = TokenAlloc(T_OPERATOR, 0, "function_form"); /* create a 'fcn_form' token */

        pID = T_POP(); /* pop ID */

        T_INSERT_TAIL_CHILD(pOp, pID); /* left child ID */

        do
        {
            Parser_Vb();
            pVb = T_POP(); /* pop Vb */
            T_INSERT_TAIL_CHILD(pOp, pVb); /* child Vb */
        } while ((T_FIRST()->type == T_IDENTIFIER) ||
                 (T_MATCH(T_FIRST(), T_PUNCTION, "(")));

        T_VERIFY(T_OPERATOR, "=");
        T_POP_DUMP(); /* dump the '=' */

        Parser_E();

        pE = T_POP(); /* pop E */

        T_INSERT_TAIL_CHILD(pOp, pE); /* right child E */
        T_PUSH(pOp); /* push tree Op */

        LOG_BUP("Db -> '<IDENTIFIER>' Vb+ '=' E");
    }
    else
    {
        printf("ERROR: syntax error at token ('%s')\n", T_SECOND()->pStr);
        exit(1);
    }
}


/*
 * Dr -> 'rec' Db   => 'rec'
 *    -> Db
 */
void Parser_Dr(void)
{
    Token * pOp;
    Token * pDb;

    if (T_MATCH(T_FIRST(), T_KEYWORD, "rec"))
    {
        LOG_TDN("Dr -> 'rec' Db");

        pOp = T_POP_OP(); /* pop 'rec' */

        Parser_Db();

        pDb = T_POP(); /* pop Db */

        T_INSERT_TAIL_CHILD(pOp, pDb); /* single child Db */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Dr -> 'rec' Db");
    }
    else
    {
        LOG_TDN("Dr -> Db");
        Parser_Db();
        LOG_BUP("Dr -> Db");
    }
}


/*
 * Da -> Dr ( 'and' Dr )+   => 'and'
 *    -> Dr
 */
void Parser_Da(void)
{
    Token * pDr;
    Token * pOp;

    LOG_TDN("Da -> Dr");
    Parser_Dr();
    LOG_BUP("Da -> Dr");

    if (T_MATCH(T_SECOND(), T_KEYWORD, "and"))
    {
        LOG_TDN("Da -> Dr ( 'and' Dr )+");

        pOp = TokenAlloc(T_OPERATOR, 0, "and"); /* create a 'and' token */

        while (T_MATCH(T_SECOND(), T_KEYWORD, "and"))
        {
            pDr = T_POP(); /* pop Dr */
            T_INSERT_TAIL_CHILD(pOp, pDr); /* child Dr */ 

            T_POP_DUMP(); /* dump the 'and' */

            Parser_Dr();
        }

        pDr = T_POP(); /* pop Dr */
        T_INSERT_TAIL_CHILD(pOp, pDr); /* child Dr */ 

        T_PUSH(pOp); /* push tree Op */

        LOG_BUP("Da -> Dr ( 'and' Dr )+");
    }
}


/*
 * D -> Da 'within' D   => 'within'
 *   -> Da
 */
void Parser_D(void)
{
    Token * pDa;
    Token * pOp;
    Token * pD;

    LOG_TDN("D -> Da");
    Parser_Da();
    LOG_BUP("D -> Da");

    while (T_MATCH(T_SECOND(), T_KEYWORD, "within"))
    {
        LOG_TDN("D -> Da 'within' D");

        pDa = T_POP(); /* pop Da */

        pOp = T_POP_OP(); /* pop 'within' */

        Parser_D();

        pD = T_POP(); /* pop D */

        T_INSERT_TAIL_CHILD(pOp, pDa); /* left child Da */
        T_INSERT_TAIL_CHILD(pOp, pD);  /* right child D */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("D -> Da 'within' D");
    }
}


/*
 * Rn -> '<IDENTIFIER>'
 *    -> '<INTEGER>'
 *    -> '<STRING>'
 *    -> 'true'           => 'true'
 *    -> 'false'          => 'false'
 *    -> 'nil'            => 'nil'
 *    -> '(' E ')'
 *    -> 'dummy'          => 'dummy'
 */
void Parser_Rn(void)
{
    Token * pE;

    if (T_FIRST()->type == T_IDENTIFIER)
    {
        /* nothing to do, leave token on the stack */
        LOG_TDN("Rn -> '<IDENTIFIER>'");
        LOG_BUP("Rn -> '<IDENTIFIER>'");
    }
    else if (T_FIRST()->type == T_INTEGER)
    {
        /* nothing to do, leave token on the stack */
        LOG_TDN("Rn -> '<INTEGER>'");
        LOG_BUP("Rn -> '<INTEGER>'");
    }
    else if (T_FIRST()->type == T_STRING)
    {
        /* nothing to do, leave token on the stack */
        LOG_TDN("Rn -> '<STRING>'");
        LOG_BUP("Rn -> '<STRING>'");
    }
    else if T_MATCH(T_FIRST(), T_KEYWORD, "true")
    {
        /* nothing to do, leave token on the stack */
        LOG_TDN("Rn -> 'true'");
        LOG_BUP("Rn -> 'true'");

        /* XXX "<true>" hack to match RPAL interpreter AST output */
        free(T_FIRST()->pStr);
        T_FIRST()->pStr = strdup("<true>");
    }
    else if T_MATCH(T_FIRST(), T_KEYWORD, "false")
    {
        /* nothing to do, leave token on the stack */
        LOG_TDN("Rn -> 'false'");
        LOG_BUP("Rn -> 'false'");

        /* XXX "<false>" hack to match RPAL interpreter AST output */
        free(T_FIRST()->pStr);
        T_FIRST()->pStr = strdup("<false>");
    }
    else if T_MATCH(T_FIRST(), T_KEYWORD, "nil")
    {
        /* nothing to do, leave token on the stack */
        LOG_TDN("Rn -> 'nil'");
        LOG_BUP("Rn -> 'nil'");

        /* XXX "<nil>" hack to match RPAL interpreter AST output */
        free(T_FIRST()->pStr);
        T_FIRST()->pStr = strdup("<nil>");
    }
    else if T_MATCH(T_FIRST(), T_KEYWORD, "dummy")
    {
        /* nothing to do, leave token on the stack */
        LOG_TDN("Rn -> 'dummy'");
        LOG_BUP("Rn -> 'dummy'");

        /* XXX "<dummy>" hack to match RPAL interpreter AST output */
        free(T_FIRST()->pStr);
        T_FIRST()->pStr = strdup("<dummy>");
    }
    else if (T_MATCH(T_FIRST(), T_PUNCTION, "("))
    {
        LOG_TDN("Rn -> '(' E ')'");

        T_POP_DUMP(); /* dump the '(' */

        Parser_E();

        pE = T_POP(); /* pop E */

        T_VERIFY(T_PUNCTION, ")");
        T_POP_DUMP(); /* dump the ')' */

        T_PUSH(pE); /* push E back on the stack */

        LOG_BUP("Rn -> '(' E ')'");
    }
}


/*
 * R -> R Rn   => 'gamma'
 *   -> Rn
 */
void Parser_R(void)
{
    Token * pR;
    Token * pRn;
    Token * pGamma;

    LOG_TDN("R -> Rn");
    Parser_Rn();
    LOG_BUP("R -> Rn");

    while (T_SECOND()                               &&
           ((T_SECOND()->type == T_IDENTIFIER)      ||
            (T_SECOND()->type == T_INTEGER)         ||
            (T_SECOND()->type == T_STRING)          ||
            T_MATCH(T_SECOND(), T_KEYWORD, "true")  ||
            T_MATCH(T_SECOND(), T_KEYWORD, "false") ||
            T_MATCH(T_SECOND(), T_KEYWORD, "nil")   ||
            T_MATCH(T_SECOND(), T_KEYWORD, "dummy") ||
            T_MATCH(T_SECOND(), T_PUNCTION, "(")))
    {
        LOG_TDN("R -> R Rn");

        pR = T_POP(); /* pop R */

        Parser_Rn();

        pRn = T_POP(); /* pop Rn */

        pGamma = TokenAlloc(T_OPERATOR, 0, "gamma"); /* create a 'gamma' token */

        T_INSERT_TAIL_CHILD(pGamma, pR);  /* left child R */ 
        T_INSERT_TAIL_CHILD(pGamma, pRn); /* right child pRn */
        T_PUSH(pGamma);                   /* push tree Op */

        LOG_BUP("R -> R Rn");
    }
}


/*
 * Ap -> Ap '@' '<IDENTIFIER>' R   => '@'
 *    -> R
 */
void Parser_Ap(void)
{
    Token * pAp;
    Token * pOp;
    Token * pId;
    Token * pR;

    LOG_TDN("Ap -> R");
    Parser_R();
    LOG_BUP("Ap -> R");

    while (T_MATCH(T_SECOND(), T_OPERATOR, "@"))
    {
        LOG_TDN("Ap -> Ap '@' '<IDENTIFIER>' R");

        pAp = T_POP(); /* pop Ap */
        pOp = T_POP(); /* pop operator */
        pId = T_POP(); /* pop id */

        Parser_R();

        pR = T_POP(); /* pop R */

        T_INSERT_TAIL_CHILD(pOp, pAp); /* left child Ap */ 
        T_INSERT_TAIL_CHILD(pOp, pId); /* middle child Id */
        T_INSERT_TAIL_CHILD(pOp, pR);  /* right child R */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Ap -> Ap '@' '<IDENTIFIER>' R");
    }
}


/*
 * Af -> Ap '**' Af   => '**'
 *    -> Ap
 */
void Parser_Af(void)
{
    Token * pAp;
    Token * pOp;
    Token * pAf;

    LOG_TDN("Af -> Ap");
    Parser_Ap();
    LOG_BUP("Af -> Ap");

    while (T_MATCH(T_SECOND(), T_OPERATOR, "**"))
    {
        LOG_TDN("Af -> Ap '**' Af");

        pAp = T_POP(); /* pop Ap */
        pOp = T_POP(); /* pop operator */

        Parser_Af();

        pAf = T_POP(); /* pop Af */

        T_INSERT_TAIL_CHILD(pOp, pAp); /* left child Ap */
        T_INSERT_TAIL_CHILD(pOp, pAf); /* right child Af */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Af -> Ap '**' Af");
    }
}


/*
 * At -> At '*' Af   => '*'
 *    -> At '/' Af   => '/'
 *    -> Af
 */
void Parser_At(void)
{
    char buf[64];
    Token * pAt;
    Token * pOp;
    Token * pAf;

    LOG_TDN("At -> Af");
    Parser_Af();
    LOG_BUP("At -> Af");

    while (T_MATCH(T_SECOND(), T_OPERATOR, "*") ||
           T_MATCH(T_SECOND(), T_OPERATOR, "/"))
    {
        strcpy(buf, T_SECOND()->pStr);

        LOG_TDN("At -> At '%s' Af", buf);

        pAt = T_POP(); /* pop At */
        pOp = T_POP(); /* pop operator */

        Parser_Af();

        pAf = T_POP(); /* pop Af */

        T_INSERT_TAIL_CHILD(pOp, pAt); /* left child At */
        T_INSERT_TAIL_CHILD(pOp, pAf); /* right child Af */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("At -> At '%s' Af", buf);
    }
}


/*
 * A -> A '+' At   => '+'
 *   -> A '-' At   => '-'
 *   ->   '+' At
 *   ->   '-' At   => 'neg'
 *   -> At
 */
void Parser_A(void)
{
    char buf[64];
    Token * pA;
    Token * pOp;
    Token * pAt;

    if (T_MATCH(T_FIRST(), T_OPERATOR, "+"))
    {
        LOG_TDN("A -> '+' At");

        T_POP_DUMP(); /* dump the '+' */

        Parser_At();

        LOG_BUP("A -> '+' At");
    }
    else if (T_MATCH(T_FIRST(), T_OPERATOR, "-"))
    {
        LOG_TDN("A -> '-' At");

        T_POP_DUMP(); /* dump the '-' */

        Parser_At();

        pAt = T_POP(); /* pop At */

        pOp = TokenAlloc(T_OPERATOR, 0, "neg"); /* create a 'neg' token */

        T_INSERT_TAIL_CHILD(pOp, pAt); /* single child At */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("A -> '-' At");
    }
    else
    {
        LOG_TDN("A -> At");
        Parser_At();
        LOG_BUP("A -> At");
    }

    while (T_MATCH(T_SECOND(), T_OPERATOR, "+") ||
           T_MATCH(T_SECOND(), T_OPERATOR, "-"))
    {
        strcpy(buf, T_SECOND()->pStr);

        LOG_TDN("A -> A '%s' At", buf);

        pA  = T_POP(); /* pop A */
        pOp = T_POP(); /* pop operator */

        Parser_At();

        pAt = T_POP(); /* pop At */

        T_INSERT_TAIL_CHILD(pOp, pA);  /* left child A */ 
        T_INSERT_TAIL_CHILD(pOp, pAt); /* right child At */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("A -> A '%s' At", buf);
    }
}


/*
 * Bp -> A ( 'gr' | '>'  ) A   => 'gr'
 *    -> A ( 'ge' | '>=' ) A   => 'ge'
 *    -> A ( 'ls' | '<'  ) A   => 'ls'
 *    -> A ( 'le' | '<=' ) A   => 'le'
 *    -> A 'eq' A              => 'eq'
 *    -> A 'ne' A              => 'ne'
 *    -> A
 */
void Parser_Bp(void)
{
    char buf[64];
    Token * pA1;
    Token * pOp;
    Token * pA2;

    LOG_TDN("Bp -> A");
    Parser_A();
    LOG_BUP("Bp -> A");

    if (T_MATCH(T_SECOND(), T_KEYWORD,  "gr") ||
        T_MATCH(T_SECOND(), T_OPERATOR, ">")  ||
        T_MATCH(T_SECOND(), T_KEYWORD,  "ge") ||
        T_MATCH(T_SECOND(), T_OPERATOR, ">=") ||
        T_MATCH(T_SECOND(), T_KEYWORD,  "ls") ||
        T_MATCH(T_SECOND(), T_OPERATOR, "<")  ||
        T_MATCH(T_SECOND(), T_KEYWORD,  "le") ||
        T_MATCH(T_SECOND(), T_OPERATOR, "<=") ||
        T_MATCH(T_SECOND(), T_KEYWORD,  "eq") ||
        T_MATCH(T_SECOND(), T_KEYWORD,  "ne"))
    {
        strcpy(buf,
               (T_MATCH(T_SECOND(), T_KEYWORD, "gr") ||
                T_MATCH(T_SECOND(), T_OPERATOR, ">"))   ? "( 'gr' | '>'  )" :
               (T_MATCH(T_SECOND(), T_KEYWORD, "ge") ||
                T_MATCH(T_SECOND(), T_OPERATOR, ">="))  ? "( 'ge' | '>=' )" :
               (T_MATCH(T_SECOND(), T_KEYWORD, "ls") ||
                T_MATCH(T_SECOND(), T_OPERATOR, "<"))   ? "( 'ls' | '<'  )" :
               (T_MATCH(T_SECOND(), T_KEYWORD, "le") ||
                T_MATCH(T_SECOND(), T_OPERATOR, "<="))  ? "( 'le' | '<=' )" :
               (T_MATCH(T_SECOND(), T_KEYWORD, "eq"))   ? "'eq'" : "'ne'");

        LOG_TDN("Bp -> A %s A", buf);

        pA1 = T_POP(); /* pop A1 */

        pOp = T_POP_OP(); /* pop operator */

        Parser_A();

        pA2 = T_POP(); /* pop A2 */

        T_INSERT_TAIL_CHILD(pOp, pA1); /* left child A1 */ 
        T_INSERT_TAIL_CHILD(pOp, pA2); /* right child A2 */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Bp -> A %s A", buf);
    }
}


/*
 * Bs -> 'not' Bp   => 'not'
 *    -> Bp
 */
void Parser_Bs(void)
{
    Token * pOp;
    Token * pBp;

    if (T_MATCH(T_FIRST(), T_KEYWORD, "not"))
    {
        LOG_TDN("Bs -> 'not' Bp");

        pOp = T_POP_OP(); /* pop 'not' */

        Parser_Bp();

        pBp = T_POP(); /* pop Bp */

        T_INSERT_TAIL_CHILD(pOp, pBp); /* single child Bp */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Bs -> 'not' Bp");
    }
    else
    {
        LOG_TDN("Bs -> Bp");
        Parser_Bp();
        LOG_BUP("Bs -> Bp");
    }
}


/*
 * Bt -> Bt '&' Bs   => '&'
 *    -> Bs
 */
void Parser_Bt(void)
{
    Token * pBt;
    Token * pOp;
    Token * pBs;

    LOG_TDN("Bt -> Bs");
    Parser_Bs();
    LOG_BUP("Bt -> Bs");

    while (T_MATCH(T_SECOND(), T_OPERATOR, "&"))
    {
        LOG_TDN("Bt -> Bt '&' Bs");

        pBt = T_POP(); /* pop Bt */

        pOp = T_POP_OP(); /* pop '&' */

        Parser_Bs();

        pBs = T_POP(); /* pop Bs */

        T_INSERT_TAIL_CHILD(pOp, pBt); /* left child Bt */ 
        T_INSERT_TAIL_CHILD(pOp, pBs); /* right child Bs */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Bt -> Bt '&' Bs");
    }
}


/*
 * B -> B 'or' Bt   => 'or'
 *   -> Bt
 */
void Parser_B(void)
{
    Token * pB;
    Token * pOp;
    Token * pBt;

    LOG_TDN("B -> Bt");
    Parser_Bt();
    LOG_BUP("B -> Bt");

    while (T_MATCH(T_SECOND(), T_KEYWORD, "or"))
    {
        LOG_TDN("B -> B 'or' Bt");

        pB = T_POP(); /* pop B */

        pOp = T_POP_OP(); /* pop 'or' */

        Parser_Bt();

        pBt = T_POP(); /* pop Bt */

        T_INSERT_TAIL_CHILD(pOp, pB);  /* left child B */ 
        T_INSERT_TAIL_CHILD(pOp, pBt); /* right child Bt */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("B -> B 'or' Bt");
    }
}


/*
 * Tc -> B '->' Tc '|' Tc   => '->'
 *    -> B
 */
void Parser_Tc(void)
{
    Token * pB;
    Token * pOp;
    Token * pTc1;
    Token * pTc2;

    LOG_TDN("Tc -> B");
    Parser_B();
    LOG_BUP("Tc -> B");

    while (T_MATCH(T_SECOND(), T_OPERATOR, "->"))
    {
        LOG_TDN("Tc -> B '->' Tc '|' Tc");

        pB = T_POP(); /* pop B */

        pOp = T_POP_OP(); /* pop '->' */

        Parser_Tc();

        pTc1 = T_POP(); /* pop Tc1 */

        T_VERIFY(T_OPERATOR, "|");
        T_POP_DUMP(); /* dump the '|' */

        Parser_Tc();

        pTc2 = T_POP(); /* pop Tc2 */

        T_INSERT_TAIL_CHILD(pOp, pB);   /* left child B */
        T_INSERT_TAIL_CHILD(pOp, pTc1); /* middle child Tc1 */
        T_INSERT_TAIL_CHILD(pOp, pTc2); /* right child Tc2 */
        T_PUSH(pOp);                    /* push tree Op */

        LOG_BUP("Tc -> B '->' Tc '|' Tc");
    }
}


/*
 * Ta -> Ta 'aug' Tc   => 'aug'
 *    -> Tc
 */
void Parser_Ta(void)
{
    Token * pTa;
    Token * pOp;
    Token * pTc;

    LOG_TDN("Ta -> Tc");
    Parser_Tc();
    LOG_BUP("Ta -> Tc");

    while (T_MATCH(T_SECOND(), T_KEYWORD, "aug"))
    {
        LOG_TDN("Ta -> Ta 'aug' Tc");

        pTa = T_POP(); /* pop Bt */

        pOp = T_POP_OP(); /* pop 'aug' */

        Parser_Tc();

        pTc = T_POP(); /* pop Tc */

        T_INSERT_TAIL_CHILD(pOp, pTa); /* left child Ta */ 
        T_INSERT_TAIL_CHILD(pOp, pTc); /* right child Tc */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Ta -> Ta 'aug' Tc");
    }
}


/*
 * T -> Ta ( ',' Ta )+   => 'tau'
 *   -> Ta
 */
void Parser_T(void)
{
    Token * pTa;
    Token * pOp;

    LOG_TDN("T -> Ta");
    Parser_Ta();
    LOG_BUP("T -> Ta");

    if (T_MATCH(T_SECOND(), T_PUNCTION, ","))
    {
        LOG_TDN("T -> Ta ( ',' Ta )+");

        pOp = TokenAlloc(T_OPERATOR, 0, "tau"); /* create a 'tau' token */

        while (T_MATCH(T_SECOND(), T_PUNCTION, ","))
        {
            pTa = T_POP(); /* pop Ta */
            T_INSERT_TAIL_CHILD(pOp, pTa); /* child Ta */ 

            T_POP_DUMP(); /* dump the ',' */

            Parser_Ta();
        }

        pTa = T_POP(); /* pop Ta */
        T_INSERT_TAIL_CHILD(pOp, pTa); /* child Ta */ 

        T_PUSH(pOp); /* push tree Op */

        LOG_BUP("T -> Ta ( ',' Ta )+");
    }
}


/*
 * Ew -> T 'where' Dr   => 'where'
 *    -> T
 */
void Parser_Ew(void)
{
    Token * pT;
    Token * pOp;
    Token * pDr;

    LOG_TDN("Ew -> T");
    Parser_T();
    LOG_BUP("Ew -> T");

    if (T_MATCH(T_SECOND(), T_KEYWORD, "where"))
    {
        LOG_TDN("Ew -> T 'where' Dr");

        pT = T_POP(); /* pop T */

        pOp = T_POP_OP(); /* pop 'where' */

        Parser_Dr();

        pDr = T_POP(); /* pop Dr */

        T_INSERT_TAIL_CHILD(pOp, pT);  /* left child T */ 
        T_INSERT_TAIL_CHILD(pOp, pDr); /* right child Dr */
        T_PUSH(pOp);                   /* push tree Op */

        LOG_BUP("Ew -> T 'where' Dr");
    }
}


/*
 * Parse an RPAL program!
 *
 * E -> 'let' D 'in' E   => 'let'
 *   -> 'fn' Vb+ '.' E   => 'lambda'
 *   -> Ew
 */
void Parser_E(void)
{
    Token * pD;
    Token * pE;
    Token * pOp;
    Token * pVb;

    if (T_MATCH(T_FIRST(), T_KEYWORD, "let"))
    {
        LOG_TDN("E -> 'let' D 'in' E");

        pOp = T_POP(); /* pop 'let' */

        Parser_D();

        pD = T_POP(); /* pop D */

        T_VERIFY(T_KEYWORD, "in");
        T_POP_DUMP(); /* dump the 'in' */

        Parser_E();

        pE = T_POP(); /* pop E */

        T_INSERT_TAIL_CHILD(pOp, pD); /* left child D */
        T_INSERT_TAIL_CHILD(pOp, pE); /* right child E */
        T_PUSH(pOp);

        LOG_BUP("E -> 'let' D 'in' E");
    }
    else if (T_MATCH(T_FIRST(), T_KEYWORD, "fn"))
    {
        LOG_TDN("E -> 'fn' Vb+ '.' E");

        T_POP_DUMP(); /* dump the 'fn' */

        pOp = TokenAlloc(T_OPERATOR, 0, "lambda"); /* create a 'lambda' token */

        do
        {
            Parser_Vb();
            pVb = T_POP(); /* pop Vb */
            T_INSERT_TAIL_CHILD(pOp, pVb); /* child Vb */
        } while ((T_FIRST()->type == T_IDENTIFIER) ||
                 (T_MATCH(T_FIRST(), T_PUNCTION, "(")));

        T_VERIFY(T_OPERATOR, ".");
        T_POP_DUMP(); /* dump the '.' */

        Parser_E();

        pE = T_POP(); /* pop E */

        T_INSERT_TAIL_CHILD(pOp, pE); /* right child E */
        T_PUSH(pOp); /* push tree Op */

        LOG_BUP("E -> 'fn' Vb+ '.' E");
    }
    else
    {
        LOG_TDN("E -> Ew");
        Parser_Ew();
        LOG_BUP("E -> Ew");
    }
}


/* Recursively print the AST tree rooted at pRoot. */
void DumpAST(Token * pRoot, int indent)
{
    Token * pChild;
    int i;

    if (pRoot == NULL) return;

    for (i = 0; i < indent; i++) printf(".");

    /* XXX trailing space hack to match RPAL interpreter AST output */
    printf("%s \n", TokenToStr(pRoot));

    for (pChild = T_FIRST_CHILD(pRoot);
         pChild != NULL;
         pChild = T_NEXT(pChild))
    {
        DumpAST(pChild, (indent + 1));
    }
}


/* Recursively free the AST tree rooted at pRoot. */
void FreeAST(Token * pRoot)
{
    Token * pChild;

    if (pRoot != NULL)
    {
        while ((pChild = T_FIRST_CHILD(pRoot)) != NULL)
        {
            T_REMOVE(pChild);
            FreeAST(pChild);
        }

        //printf("FREE: %s\n", TokenToStr(pRoot));
        TokenFree(pRoot);
    }
}


void Usage(char * pPrg)
{
    printf("Usage: %s [ -hspP ] <file>\n", pPrg);
    printf("   -h      this usage info\n");
    printf("   -s      stop after the scanner and print the tokens\n");
    printf("   -p      print production rules top down\n");
    printf("   -P      print production rules bottom up\n");
    printf("   <file>  RPAL program file\n");
    exit(1);
}


int main(int argc, char * argv[])
{
    Token * pToken;
    int scanOnly = 0;
    int fd, opt;

    TAILQ_INIT(&thead);

    while ((opt = getopt(argc, argv, "hspP")) != -1)
    {
        switch (opt)
        {
        case 's': scanOnly = 1; break;
        case 'p': log_rules |= LOG_RULE_TDN; break;
        case 'P': log_rules |= LOG_RULE_BUP; break;
        case 'h': default: Usage(argv[0]); break;
        }
    }

    if (optind == argc)
    {
        printf("ERROR: must specify input file\n");
        Usage(argv[0]);
    }

    if ((fd = open(argv[optind], O_RDONLY)) == -1)
    {
        perror("Could not open file");
        exit(1);
    }

    Scanner(fd); /* Scan the program... */

    close(fd);

    if (scanOnly)
    {
        while ((pToken = T_FIRST()) != NULL)
        {
            T_REMOVE(pToken);
            printf("%s\n", TokenToStr(pToken));
            FreeAST(pToken);
        }
    }
    else if (T_FIRST())
    {
        Parser_E(); /* Parse the program... */

        while ((pToken = T_FIRST()) != NULL)
        {
            T_REMOVE(pToken);
            if (log_rules) printf("----------\n");
            DumpAST(pToken, 0);
            FreeAST(pToken);
        }
    }
}

