//===-- M680X0CallingConv.td - Calling Conventions M680X0 --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This describes the calling conventions for the M680x0 architectures. These
/// conventions assume Int to be 4 bytes and 4 byte aligned. Short variant is
/// not supported yet.
///
//===----------------------------------------------------------------------===//

// TODO #35 Verify C convention follows SysV M68K ABI

class CCIfSubtarget<string F, CCAction A>
    : CCIf<!strconcat("static_cast<const M680X0Subtarget&>"
                       "(State.getMachineFunction().getSubtarget()).", F), A>;

//===----------------------------------------------------------------------===//
// Return Value Calling Conventions
//===----------------------------------------------------------------------===//

/// Return-value conventions common to all M680x0 CC's.
def RetCC_M680x0Common : CallingConv<[
]>;

/// M680x0 C return convention.
def RetCC_M680x0_C : CallingConv<[
  CCIfType<[i1],   CCPromoteToType<i8>>,
  CCIfType<[i8],   CCAssignToReg<[BD0, BD1]>>,
  CCIfType<[i16],  CCAssignToReg<[WD0, WD1]>>,
  CCIfType<[i32],  CCAssignToReg<[D0, D1]>>,
  CCDelegateTo<RetCC_M680x0Common>
]>;

/// M680x0 fastcc return convention.
/// TODO #36
/// This convention allows to return upto 16 bytes in registers which can be
/// split among 16 1-byte values or used for a single 16-byte value.
def RetCC_M680x0_Fast : CallingConv<[
  CCIfType<[i1],   CCPromoteToType<i8>>,
  CCIfType<[i8],   CCAssignToReg<[BD0, BD1]>>,
  CCIfType<[i16],  CCAssignToReg<[WD0, WD1, WA0, WA1]>>,
  CCIfType<[i32],  CCAssignToReg<[D0, D1, A0, A1]>>,
  CCDelegateTo<RetCC_M680x0Common>
]>;

/// This is the root return-value convention for the M680x0 backend.
def RetCC_M680x0 : CallingConv<[
  CCIfCC<"CallingConv::Fast", CCDelegateTo<RetCC_M680x0_Fast>>,
  CCDelegateTo<RetCC_M680x0_C>
]>;

//===----------------------------------------------------------------------===//
// M680x0 C Calling Convention
//===----------------------------------------------------------------------===//

/// CC_M680x0_Common - In all M680x0 calling conventions, extra integers and FP
/// values are spilled on the stack.
def CC_M680x0_Common : CallingConv<[
  /// Handles byval parameters.
  CCIfByVal<CCPassByVal<4, 4>>,

  /// Integer values get stored in stack slots that are 4 bytes in
  /// size and 4-byte aligned.
  CCIfType<[i32],  CCAssignToStack<4, 4>>
]>;

def CC_M680x0_Fast : CallingConv<[
  /// Promote i1/i8/i16 arguments to i32.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  /// The 'nest' parameter, if any, is passed in A1.
  CCIfNest<CCAssignToReg<[A1]>>, // FIXME #2 is this correct?

  /// Since M680x0 uses %An for pointers and we want them be passed in regs
  /// too we have to use custom function.
  CCIfType<[i32], CCCustom<"CC_M680x0_Any_AssignToReg">>,

  /// Otherwise, same as everything else.
  CCDelegateTo<CC_M680x0_Common>
]>;

def CC_M680x0_C : CallingConv<[
  /// Promote i1/i8/i16 arguments to i32.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  /// The 'nest' parameter, if any, is passed in A1.
  CCIfNest<CCAssignToReg<[A1]>>, // FIXME #2 is this correct?

  /// Use registers only if 'inreg' used and the call is not vararg
  CCIfNotVarArg<CCIfInReg<CCIfType<[i32], CCAssignToReg<[D0, D1]>>>>,

  /// Otherwise, same as everything else.
  CCDelegateTo<CC_M680x0_Common>
]>;

/// This is the root argument convention for the M680x0 backend.
def CC_M680x0 : CallingConv<[
  CCIfCC<"CallingConv::Fast", CCDelegateTo<CC_M680x0_Fast>>,
  CCDelegateTo<CC_M680x0_C>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved Registers.
//===----------------------------------------------------------------------===//

def CSR_NoRegs : CalleeSavedRegs<(add)>;

// A5 - BP
// A6 - FP
def CSR_STD : CalleeSavedRegs<(add D2, D3, D4, D5, D6, D7,
                                   A2, A3, A4, A5, A6)>;

