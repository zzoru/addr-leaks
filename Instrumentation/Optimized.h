#ifndef FULLPASS_H
#define FULLPASS_H

#include <llvm/Pass.h>
#include <llvm/Instruction.h>
#include <llvm/Instructions.h>
#include <llvm/GlobalVariable.h>
#include <llvm/Instructions.h>
#include <llvm/Module.h>
#include <llvm/Constant.h>
#include <llvm/Constants.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Metadata.h>
#include <llvm/Support/CFG.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Target/TargetData.h>
#include <llvm/IntrinsicInst.h>

#include "../AddrLeaks/AddrLeaks.cpp"

#include <cmath>
#include <set>
#include <map>
#include <sstream>
#include <vector>

#define null 0

using namespace llvm;

class Optimized : public ModulePass
{	
public:
	static char ID;
	Optimized();
	virtual bool runOnModule(Module&);
	
private:
	unsigned Optimized::GetSize(Type& type);
	GlobalVariable& GetParamGlobal(Argument& arg);
	Type* ConvertType(Type*);
	Function& GetAssertStringFunction();
	void HandlePrintf(CallSite*);
	void HandleUses(Value& v);
	void HandleSpecialFunctions();
	void HandleMemcpy(MemCpyInst&);
	void Setup(Module&);
	Value& Instrument(Instruction&);
	Value& Instrument(Value&);
	Function& GetAssertZeroFunction();
	Function& GetTranslateFunction();
	Value& CreateTranslateCall(Value& pointer, Instruction& before);
	Function& GetInitFunction();
	Constant& GetInt(int width, int value);
	Constant& GetAllOnesValue(Type& type);
	Constant& GetNullValue(Type& type);
	bool HasBody(Function&);
	void HandleReturns(Function&);
	GlobalVariable& GetReturnGlobal(Type&);
	GlobalVariable& GetParamShadow(Argument&);
	void AddShadow(Value&, Value&);
	Value& GetShadow(Value&);
	Function& GetMemsetFunction(int);
	void InstrumentDelayedPHINodes();
	void HandleParamPassingTo(Function&, int, Value*);
	Value* HandleExternFunctionCall(CallSite&);
	Instruction* GetNextInstruction(Instruction&);
	Function& GetCreateArgvShadowFunction();
	virtual void getAnalysisUsage(AnalysisUsage &Info) const;
	bool AlreadyInstrumented(Value&);
	void MarkAsInstrumented(Value&);
	bool MayBePointer(Value&);
	
	Module* module;
	LLVMContext* context;
	TargetData* targetData;
	std::map<Value*, Value*> valueToShadow;
	std::map<PHINode*, PHINode*> delayedPHINodes; 
	AddrLeaks* analysis;
	std::set<Value*> instrumented;
	std::set<Value*> leakedValues;

	bool dumb;
};

char Optimized::ID = 0;
static RegisterPass<Optimized> Y("leak", "Runtime leak checker", false, false);

#endif 
