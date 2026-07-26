# Decaf Compiler

Standalone Decaf compiler source and test inputs.

## Layout

- `src/` - compiler implementation, lexer, and parser grammar.
- `include/` - shared compiler headers.
- `runtime/` - Jnust runtime support used during code generation/linking.
- `tests/` - Jnust input programs restored from the course archive.
- `build/` - generated parser, lexer, and object files created by `make`.
- `bin/` - compiled `jnustcomp` executable created by `make`.

## Build

```sh
make
```

## Smoke Test

```sh
make test
```

## Clean

```sh
make clean
```
