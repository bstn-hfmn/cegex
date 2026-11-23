# cegex

**cegex** is a regular expression parser and finite automaton generator written in C.

It implements a subset of Google's [RE2 regular expression syntax](https://github.com/google/re2/wiki/Syntax), with a focus on parsing regular expressions and converting them into nondeterministic finite automata (NFAs).

The project is designed around an explicit, byte-oriented character model. Unicode is intentionally not supported.

## Features

- **Tokenization:** Converts regular expression strings into a sequence of tokens.
- **Parsing:** Parses tokens into a structured parse tree.
- **NFA generation:** Converts the parse tree into a nondeterministic finite automaton (NFA).
- **Extended ASCII support:** Supports an 8-bit character domain (values 0–255).
- **RE2-inspired syntax:** Supports most of the RE2 regular expression specification, with some exclusions, including ASCII character classes.
- **C99:** Written in standard C99 without compiler-specific language extensions.

## Architecture

The implementation is divided into three main stages:

1. **Tokenizer:** Recognizes the individual elements of a regular expression and produces tokens.
2. **Parser:** Interprets the token sequence according to the supported regular expression grammar and constructs a parse tree.
3. **NFA generator:** Traverses the parse tree and constructs an equivalent nondeterministic finite automaton.

This separation keeps lexical analysis, syntax analysis, and automaton construction independent of one another.

## Character Set

cegex operates on an 8-bit character domain, supporting values from `0` to `255`.

Unicode is explicitly out of scope. The implementation does not decode UTF-8, UTF-16, or other Unicode encodings into Unicode code points.


## Regular Expression Syntax

The supported syntax is based on Google's RE2 specification.

The goal is to implement most of the RE2 syntax while deliberately excluding certain features, including its ASCII character classes.

For the complete syntax reference, see:

- [RE2 Syntax Reference](https://github.com/google/re2/wiki/Syntax)

Note that cegex does not aim to be fully RE2-compatible.

## Building

cegex uses CMake and requires a C99-compatible C compiler.

## Scope and Limitations

cegex is not intended to be a complete replacement for established regular expression engines such as RE2.

In particular:

- Unicode support is not planned.
- Input is treated as an 8-bit character sequence.
- The supported regular expression syntax is a subset of RE2.
- Full RE2 compatibility is not a project goal.