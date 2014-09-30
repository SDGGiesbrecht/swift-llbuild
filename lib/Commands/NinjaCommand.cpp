//===-- NinjaCommand.cpp --------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Commands/Commands.h"

#include "llbuild/Ninja/Lexer.h"
#include "llbuild/Ninja/Manifest.h"
#include "llbuild/Ninja/ManifestLoader.h"
#include "llbuild/Ninja/Parser.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace llbuild;

static char hexdigit(unsigned Input) {
  return (Input < 10) ? '0' + Input : 'A' + Input;
}

static std::string escapedString(const char *Start, unsigned Length) {
  std::stringstream Result;
  for (unsigned i = 0; i != Length; ++i) {
    char c = Start[i];
    if (isprint(c)) {
      Result << c;
    } else if (c == '\n') {
      Result << "\\n";
    } else {
      Result << "\\x"
             << hexdigit(((unsigned char) c >> 4) & 0xF)
             << hexdigit((unsigned char) c & 0xF);
    }
  }
  return Result.str();
}

static void usage() {
  fprintf(stderr, "Usage: %s ninja [--help] <command> [<args>]\n",
          getprogname());
  fprintf(stderr, "\n");
  fprintf(stderr, "Available commands:\n");
  fprintf(stderr, "  lex           -- Run the Ninja lexer\n");
  fprintf(stderr, "  parse         -- Run the Ninja parser\n");
  fprintf(stderr, "  load-manifest -- Load a Ninja manifest file\n");
  fprintf(stderr, "\n");
  exit(1);
}

static std::unique_ptr<char[]> ReadFileContents(std::string Path,
                                                uint64_t* Size_Out) {

  // Open the input buffer and compute its size.
  FILE* fp = fopen(Path.c_str(), "rb");
  if (!fp) {
    fprintf(stderr, "error: %s: unable to open input: %s\n", getprogname(),
            Path.c_str());
    exit(1);
  }

  fseek(fp, 0, SEEK_END);
  uint64_t Size = *Size_Out = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // Read the file contents.
  std::unique_ptr<char[]> Data(new char[Size]);
  uint64_t Pos = 0;
  while (Pos < Size) {
    // Read data into the buffer.
    size_t Result = fread(Data.get() + Pos, 1, Size - Pos, fp);
    if (Result <= 0) {
      fprintf(stderr, "error: %s: unable to read input: %s\n", getprogname(),
              Path.c_str());
      exit(1);
    }

    Pos += Result;
  }

  return Data;
}

#pragma mark - Lex Command

static int ExecuteLexCommand(const std::vector<std::string> &Args,
                             bool LexOnly) {

  if (Args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getprogname());
    return 1;
  }

  // Read the input.
  uint64_t Size;
  std::unique_ptr<char[]> Data = ReadFileContents(Args[0], &Size);

  // Create a Ninja lexer.
  if (!LexOnly) {
      fprintf(stderr, "note: %s: reading tokens from %s\n", getprogname(),
              Args[0].c_str());
  }
  ninja::Lexer Lexer(Data.get(), Size);
  ninja::Token Tok;
  do {
    // Get the next token.
    Lexer.lex(Tok);

    if (!LexOnly) {
        std::cerr << "(Token \"" << Tok.getKindName() << "\""
                  << " String:\"" << escapedString(Tok.Start,
                                                   Tok.Length) << "\""
                  << " Length:" << Tok.Length
                  << " Line:" << Tok.Line
                  << " Column:" << Tok.Column << ")\n";
    }
  } while (Tok.TokenKind != ninja::Token::Kind::EndOfFile);

  return 0;
}

#pragma mark - Parse Command

namespace {

class ParseCommandActions : public ninja::ParseActions {
  std::string Filename;
  unsigned NumErrors = 0;
  unsigned MaxErrors = 20;
  ninja::Parser *Parser = 0;

public:
  ParseCommandActions(std::string Filename) : Filename(Filename) {}

private:
  virtual void initialize(ninja::Parser *Parser) {
    this->Parser = Parser;
  }

  virtual void error(std::string Message, const ninja::Token &At) override {
    if (NumErrors++ >= MaxErrors)
      return;

    std::cerr << Filename << ":" << At.Line << ":" << At.Column
              << ": error: " << Message << "\n";

    // Skip carat diagnostics on EOF token.
    if (At.TokenKind == ninja::Token::Kind::EndOfFile)
      return;

    // Simple caret style diagnostics.
    const char *LineBegin = At.Start, *LineEnd = At.Start,
      *BufferBegin = Parser->getLexer().getBufferStart(),
      *BufferEnd = Parser->getLexer().getBufferEnd();

    // Run line pointers forward and back.
    while (LineBegin > BufferBegin &&
           LineBegin[-1] != '\r' && LineBegin[-1] != '\n')
      --LineBegin;
    while (LineEnd < BufferEnd &&
           LineEnd[0] != '\r' && LineEnd[0] != '\n')
      ++LineEnd;

    // Show the line, indented by 2.
    std::cerr << "  " << std::string(LineBegin, LineEnd) << "\n";

    // Show the caret or squiggly, making sure to print back spaces the
    // same.
    std::cerr << "  ";
    for (const char* S = LineBegin; S != At.Start; ++S)
      std::cerr << (isspace(*S) ? *S : ' ');
    if (At.Length > 1) {
      for (unsigned i = 0; i != At.Length; ++i)
        std::cerr << '~';
    } else {
      std::cerr << '^';
    }
    std::cerr << '\n';
  }

  virtual void actOnBeginManifest(std::string Name) override {
    std::cerr << __FUNCTION__ << "(\"" << Name << "\")\n";
  }

  virtual void actOnEndManifest() override {
    std::cerr << __FUNCTION__ << "()\n";
  }

  virtual void actOnBindingDecl(const ninja::Token& Name,
                                const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << escapedString(Value.Start,
                                                 Value.Length) << "\")\n";
  }

  virtual void actOnDefaultDecl(const std::vector<ninja::Token>&
                                    Names) override {
    std::cerr << __FUNCTION__ << "(/*Names=*/[";
    bool First = true;
    for (auto Name: Names) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(Name.Start, Name.Length) << "\"";
      First = false;
    }
    std::cerr << "])\n";
  }

  virtual void actOnIncludeDecl(bool IsInclude,
                                const ninja::Token& Path) override {
    std::cerr << __FUNCTION__ << "(/*IsInclude=*/"
              << (IsInclude ? "true" : "false") << ", "
              << "/*Path=*/\"" << escapedString(Path.Start, Path.Length)
              << "\")\n";
  }

  virtual BuildResult
  actOnBeginBuildDecl(const ninja::Token& Name,
                      const std::vector<ninja::Token> &Outputs,
                      const std::vector<ninja::Token> &Inputs,
                      unsigned NumExplicitInputs,
                      unsigned NumImplicitInputs) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\""
              << ", /*Outputs=*/[";
    bool First = true;
    for (auto Name: Outputs) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(Name.Start, Name.Length) << "\"";
      First = false;
    }
    std::cerr << "], /*Inputs=*/[";
    First = true;
    for (auto Name: Inputs) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(Name.Start, Name.Length) << "\"";
      First = false;
    }
    std::cerr << "], /*NumExplicitInputs=*/" << NumExplicitInputs
              << ", /*NumImplicitInputs=*/"  << NumImplicitInputs << ")\n";
    return 0;
  }

  virtual void actOnBuildBindingDecl(BuildResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ", /*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << escapedString(Value.Start,
                                                 Value.Length) << "\")\n";
  }

  virtual void actOnEndBuildDecl(PoolResult Decl) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ")\n";
  }

  virtual PoolResult actOnBeginPoolDecl(const ninja::Token& Name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\")\n";
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ", /*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << escapedString(Value.Start,
                                                 Value.Length) << "\")\n";
  }

  virtual void actOnEndPoolDecl(PoolResult Decl) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ")\n";
  }

  virtual RuleResult actOnBeginRuleDecl(const ninja::Token& Name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\")\n";
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ", /*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << escapedString(Value.Start,
                                                 Value.Length) << "\")\n";
  }

  virtual void actOnEndRuleDecl(PoolResult Decl) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ")\n";
  }
};

}

static int ExecuteParseCommand(const std::vector<std::string> &Args) {
  if (Args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getprogname());
    return 1;
  }

  // Read the input.
  uint64_t Size;
  std::unique_ptr<char[]> Data = ReadFileContents(Args[0], &Size);

  // Run the parser.
  ParseCommandActions Actions(Args[0]);
  ninja::Parser Parser(Data.get(), Size, Actions);
  Parser.parse();

  return 0;
}

#pragma mark - Parse Only Command

namespace {

class ParseOnlyCommandActions : public ninja::ParseActions {
  ninja::Parser *Parser = 0;

public:
  ParseOnlyCommandActions() {}

private:
  virtual void initialize(ninja::Parser *Parser) {
    this->Parser = Parser;
  }

  virtual void error(std::string Message, const ninja::Token &At) override { }

  virtual void actOnBeginManifest(std::string Name) override { }

  virtual void actOnEndManifest() override { }

  virtual void actOnBindingDecl(const ninja::Token& Name,
                                const ninja::Token& Value) override { }

  virtual void actOnDefaultDecl(const std::vector<ninja::Token>&
                                    Names) override { }

  virtual void actOnIncludeDecl(bool IsInclude,
                                const ninja::Token& Path) override { }

  virtual BuildResult
  actOnBeginBuildDecl(const ninja::Token& Name,
                      const std::vector<ninja::Token> &Outputs,
                      const std::vector<ninja::Token> &Inputs,
                      unsigned NumExplicitInputs,
                      unsigned NumImplicitInputs) override {
    return 0;
  }

  virtual void actOnBuildBindingDecl(BuildResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override { }

  virtual void actOnEndBuildDecl(PoolResult Decl) override { }

  virtual PoolResult actOnBeginPoolDecl(const ninja::Token& Name) override {
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult Decl, const ninja::Token& Name,
                                    const ninja::Token& Value) override { }

  virtual void actOnEndPoolDecl(PoolResult Decl) override { }

  virtual RuleResult actOnBeginRuleDecl(const ninja::Token& Name) override {
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult Decl, const ninja::Token& Name,
                                    const ninja::Token& Value) override { }

  virtual void actOnEndRuleDecl(PoolResult Decl) override { }
};

}

static int ExecuteParseCommand(const std::vector<std::string> &Args,
                               bool ParseOnly) {
  if (Args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getprogname());
    return 1;
  }

  // Read the input.
  uint64_t Size;
  std::unique_ptr<char[]> Data = ReadFileContents(Args[0], &Size);

  // Run the parser.
  if (ParseOnly) {
      ParseOnlyCommandActions Actions;
      ninja::Parser Parser(Data.get(), Size, Actions);
      Parser.parse();
  } else {
      ParseCommandActions Actions(Args[0]);
      ninja::Parser Parser(Data.get(), Size, Actions);
      Parser.parse();
  }

  return 0;
}

#pragma mark - Load Manifest Command

namespace {

class LoadManifestActions : public ninja::ManifestLoaderActions {
private:
  virtual void error(std::string Filename, std::string Message,
                     const ninja::Token &At) override {
  }

  virtual bool readFileContents(std::string Filename,
                                const ninja::Token* ForToken,
                                std::unique_ptr<char[]> *Data_Out,
                                uint64_t *Length_Out) override {
    // FIXME: Error handling.
    *Data_Out = ReadFileContents(Filename, Length_Out);

    return true;
  };

};

}

static int ExecuteLoadManifestCommand(const std::vector<std::string> &Args) {
  if (Args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getprogname());
    return 1;
  }

  LoadManifestActions Actions;
  ninja::ManifestLoader Loader(Args[0], Actions);
  std::unique_ptr<ninja::Manifest> Manifest = Loader.load();

  // FIXME: Dump the manifest.

  return 0;
}

#pragma mark - Ninja Top-Level Command

int commands::ExecuteNinjaCommand(const std::vector<std::string> &Args) {
  // Expect the first argument to be the name of another subtool to delegate to.
  if (Args.empty() || Args[0] == "--help")
    usage();

  if (Args[0] == "lex") {
    return ExecuteLexCommand(std::vector<std::string>(Args.begin()+1,
                                                      Args.end()),
                             /*LexOnly=*/false);
  } else if (Args[0] == "lex-only") {
    return ExecuteLexCommand(std::vector<std::string>(Args.begin()+1,
                                                      Args.end()),
                             /*LexOnly=*/true);
  } else if (Args[0] == "parse") {
    return ExecuteParseCommand(std::vector<std::string>(Args.begin()+1,
                                                        Args.end()),
                               /*ParseOnly=*/false);
  } else if (Args[0] == "parse-only") {
    return ExecuteParseCommand(std::vector<std::string>(Args.begin()+1,
                                                        Args.end()),
                               /*ParseOnly=*/true);
  } else if (Args[0] == "load-manifest") {
    return ExecuteLoadManifestCommand(std::vector<std::string>(Args.begin()+1,
                                                               Args.end()));
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getprogname(),
            Args[0].c_str());
    return 1;
  }
}