//===- BuildSystem.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEM_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEM_H

#include "llbuild/Basic/Compiler.h"

#include <cstdint>
#include <string>

namespace llbuild {
namespace buildsystem {

class BuildExecutionQueue;
class Tool;
  
class BuildSystemDelegate {
  std::string name;
  uint32_t version;
  
public:
  BuildSystemDelegate(const std::string& name, uint32_t version)
      : name(name), version(version) {}
  virtual ~BuildSystemDelegate();

  /// Called by the build system to get the client name.
  const std::string& getName() const { return name; }

  /// Called by the build system to get the current client version.
  uint32_t getVersion() const { return version; }

  /// Called by the build file loader to report an error.
  //
  // FIXME: Support better diagnostics by passing a token of some kind.
  virtual void error(const std::string& filename,
                     const std::string& message) = 0;

  /// Called by the build system to get a tool definition.
  ///
  /// This method is called to look for all tools, even ones which are built-in
  /// to the BuildSystem, in order to give the client an opportunity to override
  /// built-in tools.
  ///
  /// \param name The name of the tool to lookup.
  /// \returns The tool to use on success, or otherwise nil.
  virtual std::unique_ptr<Tool> lookupTool(const std::string& name) = 0;

  /// Called by the build system to get create the object used to dispatch work.
  virtual std::unique_ptr<BuildExecutionQueue> createExecutionQueue() = 0;
};

/// The BuildSystem class is used to perform builds using the native build
/// system.
class BuildSystem {
private:
  void *impl;

public:
  /// Create a build system with the given delegate.
  ///
  /// \arg mainFilename The path of the main build file.
  explicit BuildSystem(BuildSystemDelegate& delegate,
                       const std::string& mainFilename);
  ~BuildSystem();

  /// Return the delegate the engine was configured with.
  BuildSystemDelegate& getDelegate();

  /// @name Client API
  /// @{

  /// Attach (or create) the database at the given path.
  ///
  /// \returns True on success.
  bool attachDB(const std::string& path, std::string* error_out);

  /// Enable low-level engine tracing into the given output file.
  ///
  /// \returns True on success.
  bool enableTracing(const std::string& path, std::string* error_out);

  /// Build the named target.
  ///
  /// \returns True on success.
  bool build(const std::string& target);

  /// @}
};

}
}

#endif