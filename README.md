# 🥚 Egg Compiler

> A modern compiled programming language built with C++ and LLVM.

Egg Compiler is a hobby compiler project designed to explore compiler construction, language design, and LLVM code generation. It features a complete compilation pipeline—from lexical analysis to LLVM IR generation—and aims to provide a clean, expressive systems programming language with manual memory management, structs, arrays, and native interoperability.

---

## Features

- LLVM-based backend
- Recursive descent parser
- Abstract Syntax Tree (AST)
- Detailed compiler diagnostics with source highlighting
- Structs and member access
- Static typing
- Arrays (including multidimensional arrays)
- Functions and recursion
- External C function support (`extern`)
- Variadic function declarations
- Manual heap allocation
- Pointer operations
- Integer, floating-point, character, string, and boolean types
- While loops
- Conditional statements (`if`, `else if`, `else`)
- Arithmetic and comparison operators
- LLVM IR generation

---

## Example

```egg
extern <stdio.h>

extern printf(format: charray, ...): int

struct Person {
    age: int,
    height: float
}

func factorial(n: int): int {
    if n <= 1 ? {
        return 1
    }

    return n * factorial(n - 1)
}

var person: Person
person.age = 21
person.height = 1.82

printf("Age: %d\n", person.age)
printf("5! = %d\n", factorial(5))
```

---

## Memory Management

Egg currently uses explicit memory management.

```egg
var ptr: >int = *42

printf("%d\n", <ptr)

free ptr
```

### Operators

| Operator | Description |
|----------|-------------|
| `@` | Address of |
| `<` | Dereference |
| `*` | Allocate on the heap |
| `free` | Free heap memory |

---

## Supported Types

### Primitive

- `int`
- `long`
- `float`
- `double`
- `bool`
- `char`
- `charray`
- `void`

### Composite

- Structs
- Arrays
- Multidimensional arrays
- Pointer types

---

## Compiler Architecture

```
Source Code
     │
     ▼
 Lexer
     │
     ▼
 Parser
     │
     ▼
 Abstract Syntax Tree
     │
     ▼
 LLVM IR Generator
     │
     ▼
 LLVM Optimizer
     │
     ▼
 Native Executable
```

---

## Project Structure

```
compiler/
│
├── lexer/
│   └── lexer.h
│
├── parser/
│   ├── parser.h
│   └── ASTNodes.h
│
├── codegen/
│   └── codegen.h
│
├── error/
│   └── error.h
│
└── main.cpp
```

---

## Current Features

- [x] Lexer
- [x] Parser
- [x] AST
- [x] LLVM code generation
- [x] Variables
- [x] Functions
- [x] Structs
- [x] Arrays
- [x] Member access
- [x] Pointer operations
- [x] Heap allocation
- [x] External C functions
- [x] Error reporting
- [x] Type checking
- [x] While loops
- [x] If / Else

---

## Planned Features

- [ ] For loops
- [ ] Enums
- [ ] Switch statements
- [ ] Modules and imports
- [ ] Generics
- [ ] Namespaces
- [ ] Classes
- [ ] Interfaces
- [ ] Pattern matching
- [ ] Standard library
- [ ] Better optimization passes
- [ ] Cross-platform package manager
- [ ] Language server (LSP)
- [ ] Debug information
- [ ] Better semantic analysis

---

## Dependencies

- C++20
- LLVM
- CMake

---

## Building

```bash
git clone https://github.com/<your-username>/egg-compiler.git

cd egg-compiler

mkdir build
cd build

cmake ..
cmake --build .
```

---

## Philosophy

Egg aims to be:

- Simple to learn
- Fast to compile
- Close to the hardware
- Easy to extend
- A practical playground for compiler development

The project is primarily an educational compiler exploring language implementation, LLVM, and systems programming while steadily evolving into a more capable programming language.

---

## License

This project is licensed under the MIT License.

---

<p align="center">
Built with ❤️, C++, and LLVM.
</p>
