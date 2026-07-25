#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "middlend/codegen.h"
#include "error/error.h"

// Helper function to programmatically compile C files
bool compileCFile(const std::string& sourceFile, const std::string& outputFile) {
#if defined(_WIN32)
    auto compiler = llvm::sys::findProgramByName("gcc");
    if (!compiler) compiler = llvm::sys::findProgramByName("clang");
#else
    auto compiler = llvm::sys::findProgramByName("clang");
    if (!compiler) compiler = llvm::sys::findProgramByName("gcc");
#endif

    if (!compiler) {
        std::cerr << "Error: No system C compiler (gcc/clang) found in PATH to compile " << sourceFile << ".\n";
        return false;
    }

    std::vector<llvm::StringRef> args = {
        *compiler,
        "-c",
        sourceFile,
        "-o",
        outputFile,
        "-O2"
    };

    std::string errMsg;
    bool executionFailed = false;

    std::cout << "Compiling " << sourceFile << " programmatically...\n";
    int exitCode = llvm::sys::ExecuteAndWait(
        *compiler,
        args,
        std::nullopt,
        {},
        0,
        0,
        &errMsg,
        &executionFailed
    );

    if (executionFailed || exitCode != 0) {
        std::cerr << "Failed to compile " << sourceFile << ": " << errMsg << " (Exit code: " << exitCode << ")\n";
        return false;
    }

    return true;
}

int main() {
    std::string filename = "../program";
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Error: Could not open file '" << filename << "'\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();
    file.close();

    // 1. Lexing & Parsing
    Lexer lexer(code);
    std::vector<Token> tokens = lexer.tokenize();

    ErrorReporter error_reporter = ErrorReporter(code);

    Parser parser(tokens, error_reporter);
    std::unique_ptr<ASTNode> ast = parser.parse();

    printAST(ast.get());

    // 2. Code Generation
    CodeGenerator generator = CodeGenerator(error_reporter);
    ast->codegen(generator);

    if (generator.has_errors) {
        std::cerr << "Code generation failed due to errors.\n";
        return 1;
    }

    std::string verifyErr;
    llvm::raw_string_ostream os(verifyErr);
    if (llvm::verifyModule(*generator.module, &os)) {
        std::cerr << "\n[LLVM IR ERROR] Invalid LLVM IR generated:\n";
        std::cerr << verifyErr << "\n";
        return 1;
    }

    // 3. Initialize Target Machine
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();

    llvm::Triple targetTriple(llvm::sys::getDefaultTargetTriple());
    generator.module->setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple.str(), error);

    if (!target) {
        std::cerr << "LLVM Target Lookup Error: " << error << "\n";
        return 1;
    }

    llvm::TargetOptions opt;
    auto targetMachine = target->createTargetMachine(
        targetTriple, "generic", "", opt, llvm::Reloc::PIC_
    );

    generator.module->setDataLayout(targetMachine->createDataLayout());

    // 4. Emit Main Object File (.o)
    std::string objFilename = "output.o";
    std::error_code ec;
    llvm::raw_fd_ostream dest(objFilename, ec, llvm::sys::fs::OF_None);

    if (ec) {
        std::cerr << "Could not open file: " << ec.message() << "\n";
        return 1;
    }

    llvm::legacy::PassManager pass;
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        std::cerr << "TargetMachine can't emit an object file\n";
        return 1;
    }

    pass.run(*generator.module);
    dest.flush();
    dest.close();

    std::cout << "Successfully emitted object file: " << objFilename << "\n";

    // 5. Generate & Compile Wrapper File for Registered Headers
    std::string wrapperSrc = "wrapper.c";
    std::string wrapperObj = "wrapper.o";
    const auto& headers = generator.getRegisteredHeaders();

    if (!headers.empty()) {
        std::ofstream wrapperFile(wrapperSrc);
        for (const auto& h : headers) {
            wrapperFile << "#include " << h << "\n";
        }
        wrapperFile.close();

        if (!compileCFile(wrapperSrc, wrapperObj)) {
            return 1;
        }
    }

    // 6. Programmatic Unified Linking Phase
#if defined(_WIN32)
    std::string executableName = "output.exe";
    auto linkerOrDriver = llvm::sys::findProgramByName("gcc");
    if (!linkerOrDriver) {
        linkerOrDriver = llvm::sys::findProgramByName("clang");
    }
#else
    std::string executableName = "output";
    auto linkerOrDriver = llvm::sys::findProgramByName("clang");
    if (!linkerOrDriver) {
        linkerOrDriver = llvm::sys::findProgramByName("gcc");
    }
#endif

    if (!linkerOrDriver) {
        std::cerr << "Error: No system linker/compiler (gcc/clang) found in PATH.\n";
        return 1;
    }

    std::vector<std::string> externalObjectFiles = {
        wrapperObj,
        "my_lib.o"
    };

    std::vector<std::string> extraLinkerFlags = {
        "-lm"
    };

    std::vector<llvm::StringRef> args;
    args.push_back(*linkerOrDriver);
    args.push_back(objFilename);

    for (const auto& obj : externalObjectFiles) {
        if (llvm::sys::fs::exists(obj)) {
            args.push_back(obj);
        }
    }

    args.push_back("-o");
    args.push_back(executableName);

    for (const auto& flag : extraLinkerFlags) {
        args.push_back(flag);
    }

    std::string errMsg;
    bool executionFailed = false;
    int exitCode = llvm::sys::ExecuteAndWait(
        *linkerOrDriver,
        args,
        std::nullopt,
        {},
        0,
        0,
        &errMsg,
        &executionFailed
    );

    if (executionFailed || exitCode != 0) {
        std::cerr << "Linking failed with exit code (" << exitCode << "): " << errMsg << "\n";
        return 1;
    }

    std::cout << "Successfully built executable: " << executableName << "\n";

    // 7. Run Generated Binary
#if defined(_WIN32)
    int runResult = std::system("output.exe");
#else
    int runResult = std::system("./output");
#endif

    return 0;
}