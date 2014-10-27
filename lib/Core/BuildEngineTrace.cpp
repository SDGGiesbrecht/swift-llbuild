//===-- BuildEngineTrace.cpp ----------------------------------------------===//
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

#include "BuildEngineTrace.h"

#include "llbuild/Core/BuildEngine.h"

#include <cassert>
#include <cstdio>

#include <errno.h>

using namespace llbuild;
using namespace llbuild::core;

BuildEngineTrace::BuildEngineTrace() {}

BuildEngineTrace::~BuildEngineTrace() {
  assert(!IsOpen);
}

bool BuildEngineTrace::open(const std::string& Filename,
                            std::string* Error_Out) {
  assert(!IsOpen);

  FILE *fp = fopen(Filename.c_str(), "wb");
  if (!fp) {
    *Error_Out = "unable to open '" + Filename + "' (" +
      ::strerror(errno) + ")";
    return false;
  }
  OutputPtr = fp;
  IsOpen = true;

  // Write the opening header.
  fprintf(fp, "[\n");
  return true;
}

bool BuildEngineTrace::close(std::string* Error_Out) {
  assert(IsOpen);

  FILE *FP = static_cast<FILE*>(OutputPtr);

  // Write the footer.
  fprintf(FP, "]\n");

  bool Success = fclose(FP) == 0;
  OutputPtr = nullptr;
  IsOpen = false;

  if (!Success) {
    *Error_Out = "unable to close file";
    return false;
  }

  return true;
}

#pragma mark - Tracing APIs

const char* BuildEngineTrace::getTaskName(const Task* Task) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  // See if we have already assigned a name.
  auto it = TaskNames.find(Task);
  if (it != TaskNames.end())
    return it->second.c_str();

  // Otherwise, create a name.
  char Name[64];
  sprintf(Name, "T%d", ++NumNamedTasks);
  auto Result = TaskNames.emplace(Task, Name);

  // Report the newly seen rule.
  fprintf(FP, "{ \"new-task\", \"%s\" },\n", Name);
  
  return Result.first->second.c_str();
}

const char* BuildEngineTrace::getRuleName(const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  // See if we have already assigned a name.
  auto it = RuleNames.find(Rule);
  if (it != RuleNames.end())
    return it->second.c_str();

  // Otherwise, create a name.
  char Name[64];
  sprintf(Name, "R%d", ++NumNamedRules);
  auto Result = RuleNames.emplace(Rule, Name);

  // Report the newly seen rule.
  fprintf(FP, "{ \"new-rule\", \"%s\", \"%s\" },\n", Name, Rule->Key.c_str());
  
  return Result.first->second.c_str();
}

void BuildEngineTrace::createdTaskForRule(const Task* Task,
                                          const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"created-task-for-rule\", \"%s\", \"%s\" },\n",
          getTaskName(Task), getRuleName(Rule));
}

void BuildEngineTrace::handlingTaskInputRequest(const Task* Task,
                                                const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"handling-task-input-request\", \"%s\", \"%s\" },\n",
          getTaskName(Task), getRuleName(Rule));
}

void BuildEngineTrace::readyingTaskInputRequest(const Task* Task,
                                                const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"readying-task-input-request\", \"%s\", \"%s\" },\n",
          getTaskName(Task), getRuleName(Rule));
}

void BuildEngineTrace::addedRulePendingTask(const Rule* Rule,
                                            const Task* Task) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"added-rule-pending-task\", \"%s\", \"%s\" },\n",
          getRuleName(Rule), getTaskName(Task));
}

void BuildEngineTrace::completedTaskInputRequest(const Task* Task,
                                                 const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"completed-task-input-request\", \"%s\", \"%s\" },\n",
          getTaskName(Task), getRuleName(Rule));
}

void BuildEngineTrace::updatedTaskWaitCount(const Task* Task,
                                            unsigned WaitCount) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"updated-task-wait-count\", \"%s\", %d },\n",
          getTaskName(Task), WaitCount);
}

void BuildEngineTrace::unblockedTask(const Task* Task) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"unblocked-task\", \"%s\" },\n", getTaskName(Task));
}

void BuildEngineTrace::finishedTask(const Task* Task, const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"finished-task\", \"%s\", \"%s\" },\n",
          getTaskName(Task), getRuleName(Rule));

  // Delete the task entry, as it could be reused.
  TaskNames.erase(TaskNames.find(Task));
}