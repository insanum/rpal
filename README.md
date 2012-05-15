RPAL Abstract Syntax Tree (AST) Generator
=========================================

The Right-reference Pedagogic Algorithmic Language (RPAL) is a simple
functional programming language. You can find more details on the language at
the [RPAL](http://rpal.sourceforge.net/) SourceForge site. You'll want to
review both the Lexical and Parsing Grammars for RPAL.

This application implements an RPAL parser that generates an Abstract Syntax
Tree (AST) for an RPAL program. This is for educational purposes and it does
not actually run/interpret the RPAL program.

Note that NO lex/yacc/flex/bison/etc is used here. The only semi-non-standard
dependency is the "queue.h" APIs used for managing linked lists. This header
can be found on most UNIX-like systems.

The code is very clean. If you're familiar with the Parsing Grammar you will
see that the function calls are easy to follow as the names directly coincide
with the grammar.

Included in this project are the test cases taken from Steve Walstra's
implementation of RPAL. Some of the test cases are very complex and provide a
wide coverage of functionality.

Usage
-----

```
% rpal -h
Usage: rpal [ -hspP ] <file>
   -h      this usage info
   -s      stop after the scanner and print the tokens
   -p      print production rules top down
   -P      print production rules bottom up
   <file>  RPAL program file
```

Examples for the following RPAL program (simple add):

```
% cat add
2 + 2
```

Scanner output:

```
% rpal -s add
<INT:2>
+
<INT:2>
```

Parser output:

```
% rpal add
+
.<INT:2>
.<INT:2>
```

Parser output w/ top down logging:

```
% rpal -p add
TDN: E -> Ew
TDN: Ew -> T
TDN: T -> Ta
TDN: Ta -> Tc
TDN: Tc -> B
TDN: B -> Bt
TDN: Bt -> Bs
TDN: Bs -> Bp
TDN: Bp -> A
TDN: A -> At
TDN: At -> Af
TDN: Af -> Ap
TDN: Ap -> R
TDN: R -> Rn
TDN: Rn -> '<INTEGER>'
TDN: A -> A '+' At
TDN: At -> Af
TDN: Af -> Ap
TDN: Ap -> R
TDN: R -> Rn
TDN: Rn -> '<INTEGER>'
----------
+
.<INT:2>
.<INT:2>
```

Parser output w/ bottom up logging:

```
% rpal -P add
BUP: Rn -> '<INTEGER>'
BUP: R -> Rn
BUP: Ap -> R
BUP: Af -> Ap
BUP: At -> Af
BUP: A -> At
BUP: Rn -> '<INTEGER>'
BUP: R -> Rn
BUP: Ap -> R
BUP: Af -> Ap
BUP: At -> Af
BUP: A -> A '+' At
BUP: Bp -> A
BUP: Bs -> Bp
BUP: Bt -> Bs
BUP: B -> Bt
BUP: Tc -> B
BUP: Ta -> Tc
BUP: T -> Ta
BUP: Ew -> T
BUP: E -> Ew
----------
+
.<INT:2>
.<INT:2>
```

Design
------

The program is split into two separate parts... the scanner and parser. The
scanner "tokenizes" an RPAL program based on RPAL's lexicon. The scanner has
some smarts and classifies each token into one of the following types: keyword,
identifier, integer, operator, string, and punction. Once the scanner has
created the list of tokens, the parser (based on RPAL's phrase structure
grammar) parses the token list to create an Abstract Syntax Tree (AST).

At the core of both the scanner and parser is the "Token" structure:

```C
typedef struct _token
{
    TAILQ_ENTRY(_token)         siblings;
    TokenType                   type;
    int                         length; /* pStr length w/ delim */
    char *                      pStr;
    TAILQ_HEAD(subtree, _token) children;
} Token;
```

A token will always live in a list (of siblings) and can also be the root of an
AST (has children). The scanner puts all the tokens in a list which in turn
acts as a stack from left/head (top) to right/tail (bottom). At this point all
tokens are siblings of each other.

The parser is implemented as a recursive descent parser that in turn generates
the AST from the bottom up. This is performed by continuously popping tokens
and sub-trees off the stack, building a new subtree, pushing it back on the
stack, and repeating until all the tokens have been processed and a single AST
remains. Each one of the RPAL's production groups is implemented in their own
function which can be easily followed.

Final Words...
--------------

The RPAL grammar as a whole isn't exactly exciting, intuitive, or easy to
follow... but when you trust the individual production rules the bigger picture
comes for free. Remember it's the bricks that build the house and laying each
brick is simple.

Lastly, if you've read this far I assume you're not just driving by and have a
special interest in RPAL, probably education. Don't cheat! :-)

