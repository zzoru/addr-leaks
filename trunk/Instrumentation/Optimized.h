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
	std::vector<std::pair<Instruction*, std::vector<Value*> > > getPrintfLeaks();
	void HandleSinkCalls();
	void AddAssertCode(Value& shadow, Instruction& sinkCall);
	void AddStringAssertCode(Value& shadow, Instruction& sinkCall);
	unsigned GetAllocSize(Type& type);
	GlobalVariable& GetParamGlobal(Type&, unsigned argNo);
	
	unsigned GetSize(Type& type);
	
	void HandlePrintf(CallSite*);
	void HandleUses(Value& v);
	void HandleSpecialFunctions();
	void HandleMemcpy(MemCpyInst&);
	void Setup(Module&);
	void Instrument(Instruction&);
	Function& GetAssertZeroFunction();
	Function& GetTranslateFunction();
	Value& CreateTranslateCall(Value& pointer, Instruction& before);
	Function& GetInitFunction();
	Constant& GetAllOnesValue(Type& type);
	Constant& GetNullValue(Type& type);
	bool HasBody(Function&);
	void HandleReturns(Function&);
	GlobalVariable& GetReturnGlobal(Type&);
	void AddShadow(Instruction&, Value& shadow);
	Value& GetShadow(Value&);
	void InstrumentDelayedPHINodes();
	void HandleParamPassingTo(Function&, Argument&);
	Value* HandleExternFunctionCall(CallSite&);
	Instruction* GetNextInstruction(Instruction&);
	virtual void getAnalysisUsage(AnalysisUsage &Info) const;
	bool AlreadyInstrumented(Instruction& i);
	void MarkAsInstrumented(Instruction&);
	
	Module* module;
	LLVMContext* context;
	TargetData* targetData;
	std::map<PHINode*, PHINode*> delayedPHINodes; 
	AddrLeaks* analysis;
	
	std::set<std::pair<Function*, Argument*> > callsToBeHandled;
	std::set<Function*> returnsToBeHandled;

	bool dumb;
	bool continueExecution;
};

char Optimized::ID = 0;
static RegisterPass<Optimized> Y("leak", "Runtime leak checker", false, false);

#endif 
