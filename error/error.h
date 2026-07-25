//
// Created by paikr on 7/24/2026.
//

#ifndef COMPILER_ERROR_H
#define COMPILER_ERROR_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <windows.h>

inline void enableVirtualTerminalProcessing() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);

    // Also enable for std::cerr (STD_ERROR_HANDLE)
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD dwErrMode = 0;
        if (GetConsoleMode(hErr, &dwErrMode)) {
            dwErrMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hErr, dwErrMode);
        }
    }
}

class ErrorReporter {
    std::string sourceCode;
    std::vector<std::string> lines;

public:
    explicit ErrorReporter(std::string source) : sourceCode(std::move(source)) {
        enableVirtualTerminalProcessing();
        std::stringstream ss(sourceCode);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line);
        }
    }

    void emitError(size_t line, size_t col, const std::string& title,
                   const std::string& explanation, const std::string& hint = "") {

        // ANSI Color Codes for terminal formatting
        const char* RED   = "\033[1;31m";
        const char* GREEN  = "\033[1;32m";
        const char* YELLOW  = "\033[1;33m";
        const char* BOLD  = "\033[1m";
        const char* ITALIC  = "\033[3m";
        const char* RESET = "\033[0m";

        std::cerr << RED << "[ERROR] " << BOLD << title << RESET << " @ " << line << ":" << col << "\n\n";

        // Display offending line from source code
        if (line > 0 && line <= lines.size()) {
            std::string srcLine = lines[line - 1];
            std::cerr << "  " << line << " | " << srcLine << "\n";

            // Print caret indicator (^~~~...)
            std::cerr << "    | ";
            for (size_t i = 1; i < col; ++i) {
                // Keep spacing for tabs or spaces
                std::cerr << (srcLine[i - 1] == '\t' ? '\t' : ' ');
            }
            std::cerr << RED << "^~[ERROR]" << RESET << "\n\n";
        }

        // Print verbose explanation
        std::cerr << BOLD << ITALIC << GREEN << "Explanation: " << RESET << GREEN << ITALIC << explanation << "\n";

        // Print hint / suggestion if available
        if (!hint.empty()) {
            std::cerr << YELLOW << ITALIC << "Hint: " << RESET << YELLOW << hint << "\n";
        }
        std::cerr << "\n";
    }
};

#endif //COMPILER_ERROR_H