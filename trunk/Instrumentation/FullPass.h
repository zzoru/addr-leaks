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

class FullPass : public ModulePass
{	
public:
	static char ID;
	FullPass();
	virtual bool runOnModule(Module&);
	
private:
	void HandleStores(Value& v);
	void HandleUses(Value& v);
	void HandleSpecialFunctions();
	void HandleMemcpy(MemCpyInst&);
	void Setup(Module&);
	Value& Instrument(Instruction&);
	Value& Instrument(Value&);
	Function& GetAssertZeroFunction();
	Function& GetTranslateFunction();
	Value& CreateTranslateCall(Value& pointer, Instruction& before);
//	Function& GetGetFromHashFunction();
	Function& GetInitFunction();
	Constant& GetInt(int width, int value);
	Constant& GetAllOnesValue(Type& type);
	Constant& GetNullValue(Type& type);
	bool HasBody(Function&);
	void HandleReturns(Function&);
//	void HandleParam(Argument&);
	GlobalVariable& GetReturnGlobal(Function&);
//	GlobalVariable& GetReturnShadowMemory(Type&);
	GlobalVariable& GetParamShadow(Argument&);
	void AddShadow(Value&, Value&);
	Value& GetShadow(Value&);
//	Value& GetShadowMemory(Value&);
//	void AddShadowMemory(Value&, Value&);
//	Value* CreateArgumentShadowMemory(Argument&);
//	void SetupParamPassingShadows();
//	void AddParamShadow(Argument&, GlobalVariable&);
//	GlobalVariable& GetParamGlobal(Argument&); 
	Function& GetMemsetFunction(int);
	void InstrumentDelayedPHINodes();
//	void CreateAndInsertFillMemoryCode(Value& ptr, Value& numBytes, Value& value, Instruction& insertBefore);
	void HandleParamPassingTo(Function&, int, GlobalVariable*);
	Value* HandleExternFunctionCall(CallSite&);
//	Value* HandlePrintfCall(CallSite&);
//	Value* HandleMallocCall(CallSite&);
	Instruction* GetNextInstruction(Instruction&);
	Function& GetCreateArgvShadowFunction();
	virtual void getAnalysisUsage(AnalysisUsage &Info) const;
	bool AlreadyInstrumented(Value&);
	void MarkAsInstrumented(Value&);
	bool MayBePointer(Value&);
	
	Module* module;
	LLVMContext* context;
	TargetData* targetData;
//	std::map<Value*, Value*> shadowMemoryMap;
	std::map<Value*, Value*> valueToShadow;
	std::map<PHINode*, PHINode*> delayedPHINodes; //TODO: That is not a good name but I can't think any better than this right now
//	std::map<PHINode*, PHINode*> delayedPHIShadowMemoryNodes;
	AddrLeaks* analysis;
	std::set<Value*> instrumented;
	std::set<Value*> leakedValues;
	
	static const int target = 64;
	static const int ptrSize = 64;
};

char FullPass::ID = 0;
static RegisterPass<FullPass> Y("leak", "Runtime leak checker", false, false);

#endif 
