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

#include <cmath>
#include <set>
#include <map>
#include <sstream>

#define null 0

using namespace llvm;

class FullPass : public ModulePass
{	
public:
	static char ID;
	FullPass();
	virtual bool runOnModule(Module&);
	
private:
	class ValueInfo;
	
	void Setup(Module&);
	Value* Instrument(Instruction&);
	Function& GetAssertZeroFunction();
	Function& GetAddInHashFunction();
	Function& GetGetFromHashFunction();
	Function& GetInitFunction();
	Constant& GetInt(int width, int value);
	Constant& GetAllOnesValue(Type& type);
	Constant& GetNullValue(Type& type);
	bool HasBody(Function&);
	GlobalVariable& GetReturnShadow(Type&);
	GlobalVariable& GetReturnShadowMemory(Type&);
	Value& GetShadow(Value&);
	Value& GetShadowMemory(Value&);
	void AddShadowMemory(Value&, Value&);
	void SetupParamPassingShadows();
	GlobalVariable& GetParamShadow(Function&, int); //@todo Perhaps should receive an Arg as type (I'm not sure whether Arg is really the equivalent as Param)
	void MarkAsNotOriginal(Instruction&);
	bool IsNotOriginal(Instruction&);
	Function& GetMemsetFunction(int);
	void InstrumentDelayedPHINodes();
	void CreateAndInsertFillMemoryCode(Value& ptr, Value& numBytes, Value& value, Instruction& insertBefore);
	Value* HandleExternFunctionCall(CallSite&);
	Value* HandlePrintfCall(CallSite&);
	Value* HandleMallocCall(CallSite&);
	Instruction* GetNextInstruction(Instruction&);
	Function& GetCreateArgvShadowFunction();
	
	Module* module;
	LLVMContext* context;
	TargetData* targetData;
	std::map<Function*, std::vector<GlobalVariable*> > paramsShadows;
	std::map<Value*, Value*> shadowMemoryMap;
	std::map<Value*, Value*> valueToShadow;
	std::map<PHINode*, PHINode*> delayedPHINodes; //@todo That is not a good name but I can't think any better than this right now
	std::map<PHINode*, PHINode*> delayedPHIShadowMemoryNodes;
	
	static const int target = 64;
	static const int ptrSize = 64;
};

char FullPass::ID = 0;
static RegisterPass<FullPass> X("leak", "Runtime leak checker", false, false);

#endif 
