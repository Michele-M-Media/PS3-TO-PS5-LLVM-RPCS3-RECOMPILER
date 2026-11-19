//===-- TargetSelect.cpp - Target Chooser Code ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This just asks the TargetRegistry for the appropriate target to use, and
// allows the user to specify a specific one on the commandline with -march=x,
// -mcpu=y, and -mattr=a,-b,+c. Clients should initialize targets prior to
// calling selectTarget().
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

extern "C" void recompiler_llvm_setup_step(const char* step);
extern "C" void recompiler_llvm_target_stack_begin() noexcept;
extern "C" void recompiler_llvm_target_stack_end() noexcept;
extern "C" void recompiler_llvm_exception_trace_begin() noexcept;
extern "C" void recompiler_llvm_exception_trace_end() noexcept;

namespace {
struct NativeTargetExceptionTrace {
  NativeTargetExceptionTrace() noexcept { recompiler_llvm_exception_trace_begin(); }
  ~NativeTargetExceptionTrace() { recompiler_llvm_exception_trace_end(); }
};
}

TargetMachine *EngineBuilder::selectTarget() {
  recompiler_llvm_setup_step("target selector entered");
  Triple TT;

  // MCJIT can generate code for remote targets, but the old JIT and Interpreter
  // must use the host architecture.
  if (WhichEngine != EngineKind::Interpreter && M)
    TT = M->getTargetTriple();

  return selectTarget(TT, MArch, MCPU, MAttrs);
}

/// selectTarget - Pick a target either via -march or by guessing the native
/// arch.  Add any CPU features specified via -mcpu or -mattr.
TargetMachine *EngineBuilder::selectTarget(const Triple &TargetTriple,
                              StringRef MArch,
                              StringRef MCPU,
                              const SmallVectorImpl<std::string>& MAttrs) {
  Triple TheTriple(TargetTriple);
  if (TheTriple.getTriple().empty())
    TheTriple.setTriple(sys::getProcessTriple());

  // Adjust the triple to match what the user requested.
  const Target *TheTarget = nullptr;
  if (!MArch.empty()) {
    auto I = find_if(TargetRegistry::targets(),
                     [&](const Target &T) { return MArch == T.getName(); });

    if (I == TargetRegistry::targets().end()) {
      if (ErrorStr)
        *ErrorStr = "No available targets are compatible with this -march, "
                    "see -version for the available targets.\n";
      return nullptr;
    }

    TheTarget = &*I;

    // Adjust the triple to match (if known), otherwise stick with the
    // requested/host triple.
    Triple::ArchType Type = Triple::getArchTypeForLLVMName(MArch);
    if (Type != Triple::UnknownArch)
      TheTriple.setArch(Type);
  } else {
    std::string Error;
    recompiler_llvm_setup_step("target registry lookup begin");
    TheTarget = TargetRegistry::lookupTarget(TheTriple, Error);
    recompiler_llvm_setup_step("target registry lookup returned");
    if (!TheTarget) {
      if (ErrorStr)
        *ErrorStr = Error;
      return nullptr;
    }
  }

  // Package up features to be passed to target/subtarget
  std::string FeaturesStr;
  if (!MAttrs.empty()) {
    SubtargetFeatures Features;
    for (unsigned i = 0; i != MAttrs.size(); ++i)
      Features.AddFeature(MAttrs[i]);
    FeaturesStr = Features.getString();
  }

  // Allocate a target...
  recompiler_llvm_target_stack_begin();
  recompiler_llvm_setup_step("target factory begin");
  const NativeTargetExceptionTrace trace;
  TargetMachine *Target = TheTarget->createTargetMachine(
      TheTriple, MCPU, FeaturesStr, Options, RelocModel, CMModel, OptLevel,
      /*JIT=*/true);
  recompiler_llvm_target_stack_end();
  recompiler_llvm_setup_step("target factory returned");
  Target->Options.EmulatedTLS = EmulatedTLS;

  assert(Target && "Could not allocate target machine!");
  return Target;
}
