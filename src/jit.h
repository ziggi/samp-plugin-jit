// Copyright (c) 2012, Zeex
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// // LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef JIT_H
#define JIT_H

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <amx/amx.h>
#include <AsmJit/X86/X86Assembler.h>

#if defined _MSC_VER
	#define JIT_CDECL __cdecl
	#define JIT_STDCALL __stdcall
#elif defined __GNUC__
	#define JIT_CDECL __attribute__((cdecl))
	#define JIT_STDCALL __attribute__((stdcall))
#endif

namespace jit {

enum AmxOpcode;

class AmxInstruction {
public:
	AmxInstruction();

	inline int getSize() const {
		return sizeof(cell) * (1 + operands_.size());
	}

	inline const cell getAddress() const {
		return address_; 
	}
	inline void setAddress(cell address) {
		address_ = address;
	}

	inline AmxOpcode getOpcode() const {
		return opcode_;
	}
	inline void setOpcode(AmxOpcode opcode) {
		opcode_ = opcode;
	}

	inline cell getOperand(unsigned int index = 0u) {
		assert(index >= 0 && index < operands_.size());
		return operands_[index];
	}

	inline std::vector<cell> &getOperands() {
		return operands_;
	}
	inline const std::vector<cell> &getOperands() const {
		return operands_;
	}
	inline void setOperands(std::vector<cell> operands) {
		operands_ = operands;
	}

	inline void addOperand(cell value) {
		operands_.push_back(value);
	}

	inline int getNumOperands() const {
		return operands_.size();
	}

	const char *getName() const;

private:
	cell address_;
	AmxOpcode opcode_;
	std::vector<cell> operands_;

private:
	static const char *opcodeNames[];
};

class AmxVm {
public:
	AmxVm(AMX *amx) : amx_(amx) {}

	inline AMX *getAmx() const {
		return amx_;
	}
	inline AMX_HEADER *getHeader() const {
		return reinterpret_cast<AMX_HEADER*>(amx_->base);
	}
	inline unsigned char *getData() const {
		return amx_->data != 0 ? amx_->data : amx_->base + getHeader()->dat;
	}
	inline unsigned char *getCode() const {
		return amx_->base + getHeader()->cod;
	}

	int getNumPublics() const {
		return (getHeader()->natives - getHeader()->publics) / getHeader()->defsize;
	}
	int getNumNatives() const {
		return (getHeader()->libraries - getHeader()->natives) / getHeader()->defsize;
	}

	AMX_FUNCSTUBNT *getPublics() const {
		return reinterpret_cast<AMX_FUNCSTUBNT*>(getHeader()->publics + amx_->base);
	}
	AMX_FUNCSTUBNT *getNatives() const {
		return reinterpret_cast<AMX_FUNCSTUBNT*>(getHeader()->natives + amx_->base);
	}

	cell getPublicAddress(int index) const;
	cell getNativeAddress(int index) const;

	int getPublicIndex(cell address) const;
	int getNativeIndex(cell address) const;

	const char *getPublicName(int index) const;
	const char *getNativeName(int index) const;

	cell *getStack() const {
		return reinterpret_cast<cell*>(getData() + amx_->stk);
	}

	cell *pushStack(cell value);
	cell  popStack();
	void  popStack(int ncells);

private:
	AMX *amx_;
};

class AmxDisassembler {
public:
	AmxDisassembler(AmxVm vm);

	// An opcode table maps opcodes to label addresses inside amx_Exec().
	// The GCC-specific implementation of AMX provides such a table.
	inline void setOpcodeTable(cell *opcodeTable) {
		opcodeTable_ = opcodeTable;
	}

	// Sets the instruction pointer (relative to COD).
	inline void setIp(cell ip) {
		ip_ = ip;
	}

	// Returns position of the instruction pointer.
	inline cell getIp() const {
		return ip_;
	}

	// Decodes current instruction and increments instruction pointer. 
	bool decode(AmxInstruction &instr, bool *error = 0);

private:
	AmxVm vm_;
	cell *opcodeTable_;
	cell ip_;
};

class TaggedAddress {
public:
	TaggedAddress(cell address, std::string tag = std::string())
		: address_(address), tag_(tag)
	{}

	inline cell getAddress() const { return address_; }
	inline std::string getTag() const { return tag_; }

private:
	ucell address_;
	std::string tag_;
};

static inline bool operator<(const TaggedAddress &left, const TaggedAddress &right) {
	if (left.getAddress() != right.getAddress()) {
		return left.getAddress() < right.getAddress();
	}
	return left.getTag() < right.getTag();
}

class CallContext {
public:
	explicit CallContext(AmxVm vm);
	~CallContext();

	inline cell *getParams() const {
		return params_;
	}

private:
	AmxVm vm_;
	cell *params_;
};

class Jitter {
public:
	Jitter(AMX *amx, cell *opcodeTable = 0);
	virtual ~Jitter();

	inline void *getCode() const {
		return code_;
	}
	inline std::size_t getCodeSize() const {
		return codeSize_;
	}

	inline void *getInstrPtr(cell amx_ip, void *code_ptr) {
		sysint_t native_ip = getInstrOffset(amx_ip);
		if (native_ip >= 0) {
			return reinterpret_cast<void*>(reinterpret_cast<sysint_t>(code_ptr) + native_ip);
		}
		return 0;
	}
	inline sysint_t getInstrOffset(cell amx_ip) {
		CodeMap::const_iterator iterator = codeMap_.find(amx_ip);
		if (iterator != codeMap_.end()) {
			return iterator->second;
		}
		return -1;
	}

	// Compilation error handler.
	typedef void (*CompileErrorHandler)(const AmxVm &vm, const AmxInstruction &instr);

	// Compiles the whole AMX and optionally outputs assembly code listing to the 
	// specified stream.
	bool compile(CompileErrorHandler errorHandler = 0);

	// Calls a function and returns one of AMX error codes.
	int callFunction(cell address, cell *retval);

	// Calls a public function and returns one of AMX error codes. 
	int callPublicFunction(int index, cell *retval);

private:
	Jitter(const Jitter &);
	Jitter &operator=(const Jitter &);

private:
	AmxVm vm_;

	cell *opcodeTable_;

	void *code_;
	std::size_t codeSize_;

	void *ebp_;
	void *esp_;

	void *haltEbp_;
	void *haltEsp_;

	typedef cell (JIT_CDECL *CallFunctionHelper)(void *start);
	CallFunctionHelper callFunctionHelper_;

	typedef std::map<cell, sysint_t> CodeMap;
	CodeMap codeMap_;

	typedef std::map<TaggedAddress, AsmJit::Label> LabelMap;
	LabelMap labelMap_;

private:
	// Sets a label. Optionally takes a label name.
	AsmJit::Label &L(AsmJit::X86Assembler &as, cell address, const std::string &name = "");

	// Jumps to specific AMX instruction.
	static void JIT_STDCALL doJump(Jitter *jitter, cell ip, void *stack);

	// Call native function by index in EAX).
	static cell JIT_STDCALL doSysreq(Jitter *jitter, int index, cell *params);

	// Stop execution of the current function.
	static void JIT_STDCALL doHalt(Jitter *jitter, int errorCode);

private:
	typedef void (Jitter::*IntrinsicImpl)(AsmJit::X86Assembler &as);

	struct Intrinsic {
		std::string   name;
		IntrinsicImpl impl;
	};

	static Intrinsic intrinsics_[];

	// Optimized versions of the floating-point natives.
	void native_float(AsmJit::X86Assembler &as);
	void native_floatabs(AsmJit::X86Assembler &as);
	void native_floatadd(AsmJit::X86Assembler &as);
	void native_floatsub(AsmJit::X86Assembler &as);
	void native_floatmul(AsmJit::X86Assembler &as);
	void native_floatdiv(AsmJit::X86Assembler &as);
	void native_floatsqroot(AsmJit::X86Assembler &as);
	void native_floatlog(AsmJit::X86Assembler &as);

private:
	// Halt current function (jump to the point of call).
	// Affects registers: EBP, ESP.
	void halt(AsmJit::X86Assembler &as, cell errorCode);

	// Save stk/frm and switch to real stack.
	// Affects registers: EDX, EBP, ESP.
	void beginExternalCode(AsmJit::X86Assembler &as);

	// Save ebp_/esp_ and switch to AMX stack.
	// Affects registers: EDX, EBP, ESP.
	void endExternalCode(AsmJit::X86Assembler &as);
};

} // namespace jit

#endif // !JIT_H