#include "FullPass.h"

#include <llvm/Support/raw_ostream.h>
#define db(x) errs() << x << "\n";

/*********************************************************************************************************************************************************************************
 * PUBLIC METHODS IMPLEMENTATION
 *********************************************************************************************************************************************************************************/
FullPass::FullPass() : ModulePass(ID)
{

}

bool FullPass::runOnModule(Module& module)
{
	Setup(module);
	
	for (Module::iterator it = module.begin(), itEnd = module.end(); it != itEnd; ++it)
	{	
		for (inst_iterator instructionIt = inst_begin(it), instructionItEnd = inst_end(it); instructionIt != instructionItEnd; ++instructionIt)
		{
			Instruction* i = &*instructionIt; //@todo This was necessary before. Not sure if it's necessary now.
			if (! IsNotOriginal(*i))
			{
				db("Instrumenting " << *i);
				Instrument(*instructionIt);
			}
		}
	}
	
	InstrumentDelayedPHINodes();

	return true;
}

/*********************************************************************************************************************************************************************************
 * PRIVATE METHODS IMPLEMENTATION
 *********************************************************************************************************************************************************************************/
//@todo Look whether while loops (anything that depends on itself would work) would break this

void FullPass::Setup(Module& module)
{
	this->module = &module;
	this->context = &module.getContext();
	targetData = new TargetData(&module);
	SetupParamPassingShadows();
	
	Function& f = GetInitFunction();
	
	//@todo This is not the best way to do it
	Module::iterator it = module.begin();
	inst_iterator instructionIt = inst_begin(it);
	
	CallInst* init = CallInst::Create(&f, "", &*instructionIt);
	MarkAsNotOriginal(*init);
}

Value* FullPass::Instrument(Instruction& instruction)
{
	db("*********************" << instruction)
	//@todo Make tests with type conversions including floats
	if (instruction.isBinaryOp())
	{
		BinaryOperator& bin = cast<BinaryOperator>(instruction);
		Value& op1 = *bin.getOperand(0);
		Value& op2 = *bin.getOperand(1);
		Value& shadow1 = GetShadow(op1);
		Value& shadow2 = GetShadow(op2);
		Instruction* newShadow = BinaryOperator::Create(Instruction::Or, &shadow1, &shadow2, "", &instruction);
		MarkAsNotOriginal(*newShadow);
		valueToShadow.insert(std::pair<Value*, Value*>(&instruction, newShadow));
		return newShadow;
	}

	
//    
	Value* newShadow = 0;

	switch (instruction.getOpcode())
	{
		//@todo Handle vector operations
		case Instruction::Ret:
		{
			ReturnInst& ret = cast<ReturnInst>(instruction);
			if (ret.getNumOperands() == 0) break;
			
			Value& op = *ret.getOperand(0);
			Value& shadow = GetShadow(op);
			GlobalVariable& returnShadow = GetReturnShadow(*shadow.getType());
			StoreInst* store = new StoreInst(&shadow, &returnShadow, &instruction); //@todo memory leak	
			MarkAsNotOriginal(*store);
			break;
		}
		case Instruction::Invoke:
		{
			//@todo I'll do this later
			break;
		}
		case Instruction::Resume:
		{
			//@todo I'll do this later
			break;
		}
		case Instruction::ExtractValue: //@todo Remember to test this
		{
			ExtractValueInst& extract = cast<ExtractValueInst>(instruction);
			Value* agg = extract.getAggregateOperand();
			Value& shadow = GetShadow(*agg);
			newShadow = ExtractValueInst::Create(&shadow, extract.getIndices(), "", &instruction);
			break;
		}
		case Instruction::InsertValue: //@todo Remember to test this
		{
			InsertValueInst& insert = cast<InsertValueInst>(instruction);
			Value* agg =  insert.getAggregateOperand();
			Value& aggShadow = GetShadow(*agg);
			Value* insertedValue = insert.getInsertedValueOperand();
			Value& insertedValueShadow = GetShadow(*insertedValue);
			newShadow = InsertValueInst::Create(&aggShadow, &insertedValueShadow, insert.getIndices(), "", &instruction);
			break;
		}
		case Instruction::Alloca:
		{
			//@todo Add align info (I'm not sure of how relevant this is)
			AllocaInst& alloca = cast<AllocaInst>(instruction);
			
			newShadow = &GetAllOnesValue(*alloca.getType());
			
			//duplicating the alloca (creating the shadow memory)
			AllocaInst* allocaClone = new AllocaInst(alloca.getAllocatedType(), alloca.getArraySize(), "", &instruction);
			MarkAsNotOriginal(*allocaClone);
			
//			int typeSizeInBytes = targetData->getTypeStoreSize(alloca.getAllocatedType());
//			Value* arraySize = alloca.getArraySize();
//			int arraySizeSizeInBits = targetData->getTypeSizeInBits(arraySize->getType());
//			BinaryOperator* mul = BinaryOperator::Create(Instruction::Mul, arraySize, &GetInt(arraySizeSizeInBits, typeSizeInBytes), "", &instruction);
//			MarkAsNotOriginal(*mul);
//			CreateAndInsertFillMemoryCode(*allocaClone, *mul, GetInt(8,0), instruction);
			
			AddShadowMemory(alloca, allocaClone);
			
			//storing the relationship memory <---> shadow memory in the hash table
			Function& addInHashFunction = GetAddInHashFunction();
			std::vector<Value*> args;	
			Instruction* nextInstruction = GetNextInstruction(instruction);
			BitCastInst* cast = new BitCastInst(&alloca, Type::getInt8PtrTy(*context), "", nextInstruction);
			MarkAsNotOriginal(*cast);
			args.push_back(cast);
			BitCastInst* castClone = new BitCastInst(allocaClone, Type::getInt8PtrTy(*context), "", nextInstruction);
			MarkAsNotOriginal(*castClone);
			args.push_back(castClone);
			CallInst* call = CallInst::Create(&addInHashFunction, args, "", nextInstruction); //@todo Memory leak
			MarkAsNotOriginal(*call);
			break;
		}
		case Instruction::Load:
		{
			LoadInst& load = cast<LoadInst>(instruction);
			
			Value* pointer = load.getPointerOperand();
			Value* pointerShadowMemory = GetShadowMemory(*pointer);
			
			if (pointerShadowMemory)
			{
				newShadow = new LoadInst(pointerShadowMemory, "", &instruction);
			}
			else //this is an external arg like argv
			{
				newShadow = &GetNullValue(*load.getType());
			}
			
			if (load.getType()->isPointerTy())
			{
				if (! pointerShadowMemory) //is something like argv (an memory area that does not have a shadow memory) 
				{
					
					break;
				}
				//getting from the hash the pointer to the shadow memory relative to the result of the load
				Function& f = GetGetFromHashFunction();
				std::vector<Value*> args;
				Instruction* nextInstruction = GetNextInstruction(instruction);
				BitCastInst* cast = new BitCastInst(&load, Type::getInt8PtrTy(*context), "", nextInstruction);
				MarkAsNotOriginal(*cast);
				args.push_back(cast);
				CallInst* call = CallInst::Create(&f, args, "", nextInstruction);
				MarkAsNotOriginal(*call);
				BitCastInst* res = new BitCastInst(call, load.getType(), "", nextInstruction);
				MarkAsNotOriginal(*res);
				AddShadowMemory(load, res);
			}
			
			break;
		}
		case Instruction::Store:
		{
			StoreInst& store = cast<StoreInst>(instruction);
			Value* pointer = store.getPointerOperand();
			Value* pointerShadow = GetShadowMemory(*pointer);
			Value* storedValue = store.getValueOperand();
			Value& storedValueShadow = GetShadow(*storedValue);
			StoreInst* shadowStore = new StoreInst(&storedValueShadow, pointerShadow, &instruction); //@todo Memory leak
			MarkAsNotOriginal(*shadowStore);
			break;
		}
		case Instruction::Fence:
		{
			//@todo do later
			break;
		}
//		case Instruction::CMPXCHG:
//		{
//			@todo Do this later
//			break;
//		}
		//@todo atomic read write in memory
		case Instruction::GetElementPtr:
		{
			GetElementPtrInst& gep = cast<GetElementPtrInst>(instruction);
			Value* pointer = gep.getPointerOperand();
			Value* pointerShadowMemory = GetShadowMemory(*pointer);
			
			std::vector<Value*> idx;
			
			for (GetElementPtrInst::op_iterator it = gep.idx_begin(), itEnd = gep.idx_end(); it != itEnd; ++it)
			{
				idx.push_back(*it);
			}
			
			if (pointerShadowMemory)
			{
				GetElementPtrInst* shadowGep = GetElementPtrInst::Create(pointerShadowMemory, idx, "", &instruction); //@todo Memory leak
				MarkAsNotOriginal(*shadowGep);
				AddShadowMemory(gep, shadowGep);
			}
			else
			{
				AddShadowMemory(gep, 0);
			}
				
			newShadow = &GetAllOnesValue(*gep.getType());
			
			db("Shadow of gep: " << *newShadow)
			
			break;
		}
		//@todo conversion operators
		case Instruction::BitCast:
		{
			BitCastInst& bitcast = cast<BitCastInst>(instruction);
			Value& shadow = GetShadow(*bitcast.getOperand(0));
			newShadow = new BitCastInst(&shadow, bitcast.getDestTy(), "", &instruction);
			
			if (bitcast.getOperand(0)->getType()->isPointerTy())
			{
				Value* shadowMemory = GetShadowMemory(*bitcast.getOperand(0));
				BitCastInst* newShadowMemory = new BitCastInst(shadowMemory, bitcast.getDestTy(), "", &instruction);
				AddShadowMemory(bitcast, newShadowMemory);
				MarkAsNotOriginal(*newShadowMemory);
			}
			
			break;
		}
		case Instruction::ICmp:
		case Instruction::FCmp:
		{
			//@todo buggy because the ICmp instruction may also result in an array of booleans. I need to look whether it receives as arg an vector or 
			//      integer/pointer an return either a vector or integer/pointer
			newShadow = &GetInt(1,0);
			break;
		}
		case Instruction::PHI:
		{
			//@todo Implement what's left to be implemented
			PHINode& phi = cast<PHINode>(instruction);
			//@todo This feels too much strange.
			newShadow = PHINode::Create(phi.getType(), 0, "", &instruction);  //@todo I put NumReservedValues=0 because it's the safe choice and I don't know how to get the original one
			delayedPHINodes.insert(std::pair<PHINode*, PHINode*>(&phi, cast<PHINode>(newShadow)));
			
//			if (phi.getType()->isPointerTy())
//			{
//				PHINode* newMemoryShadow = PHINode::Create(phi.getType(), 0, "", &instruction); 
//			}
//			
			break;
		}
		case Instruction::Select:
		{
			SelectInst& select = cast<SelectInst>(instruction);
			Value& trueCondValueShadow = GetShadow(*select.getTrueValue());
			Value& falseCondValueShadow = GetShadow(*select.getFalseValue());
			newShadow = SelectInst::Create(select.getCondition(), &trueCondValueShadow, &falseCondValueShadow, "", &instruction);
			break;
		}
		case Instruction::Call:
		{

			CallInst& call = cast<CallInst>(instruction);
			Function& func = *call.getCalledFunction();


			if (func.getBasicBlockList().size() == 0)
			{	
				CallSite cs(&call);
				newShadow = HandleExternFunctionCall(cs);
				break;
			}
			else
			{	
				for (int i = 0; i < call.getNumArgOperands(); i++)
				{
					Value* arg = call.getArgOperand(i);
					Value& argShadow = GetShadow(*arg);
					GlobalVariable& paramShadow = GetParamShadow(func, i);
					StoreInst* store = new StoreInst(&argShadow, &paramShadow, &instruction); //@todo memory leak
					MarkAsNotOriginal(*store);
				}

				if (call.getType()->isVoidTy()) break;
	
				BasicBlock::iterator it(&instruction);
				it++;
				Value& returnShadow = GetReturnShadow(*func.getReturnType());
				newShadow = new LoadInst(&returnShadow, "", it); //@todo Feels a bit strange but it was copied from LLVM doc so it's probably right
			}


			break;
		}
		case Instruction::Trunc:    
		{
			TruncInst& trunc = cast<TruncInst>(instruction);
			Value& shadow = GetShadow(*trunc.getOperand(0));
			newShadow = new TruncInst(&shadow, trunc.getDestTy(), "", &instruction);
			break;
		}
		case Instruction::ZExt:     
		{
			ZExtInst& zext = cast<ZExtInst>(instruction);
			Value& shadow = GetShadow(*zext.getOperand(0));
			newShadow = new ZExtInst(&shadow, zext.getDestTy(), "", &instruction);
			break;
		}
		case Instruction::SExt:      
		{
			SExtInst& sext = cast<SExtInst>(instruction);
			Value& shadow = GetShadow(*sext.getOperand(0));
			newShadow = new SExtInst(&shadow, sext.getDestTy(), "", &instruction);
			break;
		}
		//@todo Do those later
//		case Instruction::FPTrunc:   
//		case Instruction::FPExt:     
//		case Instruction::FPToUI:    
//		case Instruction::FPToSI:   
//		case Instruction::UIToFP:    
		case Instruction::SIToFP:   
		{
			SIToFPInst& conv = cast<SIToFPInst>(instruction);
			newShadow = new BitCastInst(conv.getOperand(0), conv.getDestTy(), "", &instruction);
			break;
		}
		case Instruction::IntToPtr:  
		{
			IntToPtrInst& conv = cast<IntToPtrInst>(instruction);
			newShadow = new IntToPtrInst(conv.getOperand(0), conv.getDestTy(), "", &instruction);
			break;
		}
		case Instruction::PtrToInt: 
		{
			PtrToIntInst& conv = cast<PtrToIntInst>(instruction);
			newShadow = new PtrToIntInst(conv.getOperand(0), conv.getDestTy(), "", &instruction);
			break;
		} 
		//@todo va_arg
		//@todo landingpad
		default:
		{
			break;
		}
	}
	

	if (newShadow != 0)
	{
		valueToShadow.insert(std::pair<Value*, Value*>(&instruction, newShadow));
		Instruction* i = dynamic_cast<Instruction*>(newShadow);

		if (i)
		{
			MarkAsNotOriginal(*i);
		}
	}

	
	return newShadow;
}

Function& FullPass::GetAssertZeroFunction()
{
	static Function* assertZero = 0;
	
	if (assertZero != 0) return *assertZero;
	
	std::vector<Type*> assertParams;
	assertParams.push_back(Type::getInt1Ty(*context));
	assertZero = Function::Create(FunctionType::get(Type::getVoidTy(*context),
			assertParams,
			false),
			GlobalValue::InternalLinkage,
			"assertZero",
			module);
	Function* quitProgram = Function::Create(FunctionType::get(Type::getVoidTy(*context), false),
			GlobalValue::ExternalLinkage,
			"abort",
			module);

	BasicBlock* entry = BasicBlock::Create(*context, "entry", assertZero);
	Function::ArgumentListType& argList = assertZero->getArgumentList();
	ICmpInst* cmp = new ICmpInst(*entry, ICmpInst::ICMP_EQ, &argList.front(), ConstantInt::get(Type::getInt1Ty(*context), 0));
	MarkAsNotOriginal(*cmp);

	BasicBlock* ifTrue = BasicBlock::Create(*context, "", assertZero);
	ReturnInst* ret = ReturnInst::Create(*context, ifTrue);
	MarkAsNotOriginal(*ret);
	
	BasicBlock* ifFalse = BasicBlock::Create(*context, "", assertZero);

	CallInst* call = CallInst::Create(quitProgram, "", ifFalse);
	MarkAsNotOriginal(*call);

	UnreachableInst* unr = new UnreachableInst(*context, ifFalse);
	MarkAsNotOriginal(*unr);
	
	BranchInst* branch = BranchInst::Create(ifTrue, ifFalse, cmp, entry);
	MarkAsNotOriginal(*branch);
	
	return GetAssertZeroFunction();
}

Function& FullPass::GetAddInHashFunction()
{
	static Function* func = 0;
	
	if (func) return *func;
	
	std::vector<Type*> args;
	args.push_back(Type::getInt8PtrTy(*context));
	args.push_back(Type::getInt8PtrTy(*context));
	func = Function::Create(FunctionType::get(Type::getVoidTy(*context), args, false), GlobalValue::ExternalLinkage, "addInHash", module);
	return *func;
}

Function& FullPass::GetGetFromHashFunction()
{
	static Function* func = 0;

	if (func) return *func;

	std::vector<Type*> args;
	args.push_back(Type::getInt8PtrTy(*context));
	func = Function::Create(FunctionType::get(Type::getInt8PtrTy(*context), args, false), GlobalValue::ExternalLinkage, "getFromHash", module);
	return *func;
}

Function& FullPass::GetInitFunction()
{
	static Function* func = 0;

	if (func) return *func;

	func = Function::Create(FunctionType::get(Type::getVoidTy(*context), false), GlobalValue::ExternalLinkage, "init", module);
	return *func;
}
	
Constant& FullPass::GetInt(int width, int value)
{
	ConstantInt& constant = *ConstantInt::get(Type::getIntNTy(*context, width), value, true);
	return constant;
}

Constant& FullPass::GetAllOnesValue(Type& type)
{
	int numBits = targetData->getTypeSizeInBits(&type);
	uint64_t val = 0;
	APInt ap(numBits, ~val, false);
	return *Constant::getIntegerValue(&type, ap);
}

Constant& FullPass::GetNullValue(Type& type)
{
	int numBits = targetData->getTypeSizeInBits(&type);
	uint64_t val = 0;
	APInt ap(numBits, val, false);
	return *Constant::getIntegerValue(&type, ap);
}

//ConstantInt& FullPass::Get1FilledInt(int width)
//{
//	return GetInt(width, pow(2, width) - 1);
//}

bool FullPass::HasBody(Function& f)
{
	return f.getBasicBlockList().size() == 0;
}

GlobalVariable& FullPass::GetReturnShadow(Type& type)
{
	static std::map<Type*, GlobalVariable*> returnShadows;
	
	std::map<Type*, GlobalVariable*>::iterator it = returnShadows.find(&type);
	GlobalVariable* returnShadow;
	
	if (it == returnShadows.end())
	{
		returnShadow = new GlobalVariable(*module, &type, false, GlobalValue::CommonLinkage, &GetNullValue(type), "");
		returnShadows.insert(std::pair<Type*, GlobalVariable*>(&type, returnShadow));
	}
	else
	{
		returnShadow = it->second;
	}
	
	return *returnShadow;
}

GlobalVariable& FullPass::GetReturnShadowMemory(Type& type)
{
	static std::map<Type*, GlobalVariable*> returnShadows;

	std::map<Type*, GlobalVariable*>::iterator it = returnShadows.find(&type);
	GlobalVariable* returnShadow;

	if (it == returnShadows.end())
	{
		returnShadow = new GlobalVariable(*module, &type, false, GlobalValue::CommonLinkage, &GetNullValue(type), "");
		returnShadows.insert(std::pair<Type*, GlobalVariable*>(&type, returnShadow));
	}
	else
	{
		returnShadow = it->second;
	}

	return *returnShadow;
}

Value& FullPass::GetShadow(Value& value)
{
	GlobalVariable* gv = dyn_cast<GlobalVariable>(&value);

	if (gv)
	{
		return GetAllOnesValue(*gv->getType());
	}

	Constant* constant = dyn_cast<Constant>(&value);

	if (constant)
	{
		return GetNullValue(*constant->getType());
	}
	
	Argument* argument = dyn_cast<Argument>(&value);
	
	if (argument && argument->getParent()->getName().equals("main"))
	{
		if (argument->getType()->getScalarSizeInBits() == 32)
		{
			return GetInt(32,0);
		}
		else
		{
			return GetAllOnesValue(*argument->getType());
		}
	}
	
	std::map<Value*, Value*>::iterator it = valueToShadow.find(&value);
	
	if (it != valueToShadow.end())
	{
		return *it->second;
	}
	
	Instruction* instruction = dyn_cast<Instruction>(&value);
	
	if (instruction)
	{
		Value* shadow = Instrument(*instruction);
		if (shadow == 0) db("ERROR*******************" << value);
		assert(shadow != 0);
		return *shadow;
	}
	
	assert(0);
	return value; //@todo remove this
}

//@todo If there's an infinite loop then this is a probable cause
Value* FullPass::GetShadowMemory(Value& value)
{
	if (IsExternalArg(value)) return 0;
	
	std::map<Value*, Value*>::iterator it = shadowMemoryMap.find(&value);
	
	if (it != shadowMemoryMap.end())
	{
		return it->second;
	}

	Instruction* instruction = dyn_cast<Instruction>(&value);

	if (instruction)
	{
		Instrument(*instruction);
		return GetShadowMemory(value);
	}
	
	if (dyn_cast<Argument>(&value))
	{
		db("huahahauhauhahuahuahuhuauhauhauh")
		return &GetNullValue(*value.getType());
	}
	
	GlobalVariable& gv = cast<GlobalVariable>(value);
	GlobalVariable* clone = new GlobalVariable(*module, gv.getType(), gv.isConstant(), gv.getLinkage(), &GetNullValue(*gv.getType()->getElementType()), "", &gv, gv.isThreadLocal()); 
	shadowMemoryMap.insert(std::pair<Value*, Value*>(&gv, clone));
	return clone;
}

void FullPass::AddShadowMemory(Value& original, Value* shadow)
{
	shadowMemoryMap.insert(std::pair<Value*, Value*>(&original, shadow));
}

void FullPass::SetupParamPassingShadows()
{
	for (Module::iterator it = module->begin(), itEnd = module->end(); it != itEnd; ++it)
	{
		Function& func = *it;
		
		if (func.getBasicBlockList().size() == 0 || func.getName().equals("main")) continue;
		
		inst_iterator firstInst = inst_begin(&func);
		
		Function::ArgumentListType& params = func.getArgumentList();
		std::vector<GlobalVariable*> vec;
	
		
		for (Function::ArgumentListType::iterator paramIt = params.begin(), paramItEnd = params.end(); paramIt != paramItEnd; ++paramIt)
		{
			GlobalVariable* gv = new GlobalVariable(*module, paramIt->getType(), false, GlobalValue::CommonLinkage, &GetNullValue(*paramIt->getType()), "");
			vec.push_back(gv);
			LoadInst* argShadow = new LoadInst(gv, "", &*firstInst); //@todo memory leak? 
			                                   //@todo This &* looks horrible but seems to be necessary. Perhaps there is a better way
			MarkAsNotOriginal(*argShadow);
			valueToShadow.insert(std::pair<Value*, Value*>(paramIt, argShadow));
		}
		
		paramsShadows.insert(std::pair<Function*, std::vector<GlobalVariable*> >(&func, vec));
	}
}

GlobalVariable& FullPass::GetParamShadow(Function& func, int i)
{
	std::map<Function*, std::vector<GlobalVariable*> >::iterator it = paramsShadows.find(&func);
	
	assert(it != paramsShadows.end());
	
	std::vector<GlobalVariable*> v = it->second;
	return *v[i];
}

void FullPass::MarkAsNotOriginal(Instruction& inst)
{
	inst.setMetadata("new-inst", MDNode::get(*context, std::vector<Value*>()));
}
bool FullPass::IsNotOriginal(Instruction& inst)
{
	return inst.getMetadata("new-inst") != 0;
}

Function& FullPass::GetMemsetFunction(int bitSize)
{
	//@todo I'm assuming that the length needs at most 32 bits
	static std::map<int, Function*> memsetMap;
	
	std::map<int, Function*>::iterator it = memsetMap.find(bitSize);
	
	if (it != memsetMap.end())
	{
		return *it->second;
	}
	
	/*
	 * i8* <dest>, i8 <val>, i32 <len>, i32 <align>, i1 <isvolatile>
	 */
	std::vector<Type*> params;
	params.push_back(Type::getInt8PtrTy(*context));
	params.push_back(Type::getInt8Ty(*context));
	params.push_back(Type::getIntNTy(*context, bitSize));
	params.push_back(Type::getInt32Ty(*context));
	params.push_back(Type::getInt1Ty(*context));
	
	
	std::stringstream memsetName;
	memsetName << "llvm.memset.p0i8.i" << bitSize;
	Function* memset = Function::Create(FunctionType::get(Type::getVoidTy(*context), params, false), GlobalValue::ExternalLinkage, memsetName.str(), module);
	memsetMap.insert(std::pair<int, Function*>(bitSize, memset));
	return *memset;
}

void FullPass::InstrumentDelayedPHINodes()
{
	for (std::map<PHINode*, PHINode*>::iterator it = delayedPHINodes.begin(), itEnd = delayedPHINodes.end(); it != itEnd; ++it)
	{
		PHINode& original = *it->first;
		PHINode& phiShadow = *it->second;
		
		PHINode::block_iterator blockIt = original.block_begin(), blockItEnd = original.block_end();
		for (PHINode::op_iterator it = original.op_begin(), itEnd = original.op_end(); it != itEnd; ++it)
		{
			Value& shadow = GetShadow(**it);
			phiShadow.addIncoming(&shadow, *blockIt);
			++blockIt;
		}
		
		if (original.getType()->isPointerTy())
		{
			PHINode* shadowMemoryPHI = PHINode::Create(original.getType(), 0, "", &original);
			MarkAsNotOriginal(*shadowMemoryPHI);
			
			PHINode::block_iterator blockIt = original.block_begin(), blockItEnd = original.block_end();
			for (PHINode::op_iterator it = original.op_begin(), itEnd = original.op_end(); it != itEnd; ++it)
			{
				Value* shadowMemory = GetShadowMemory(**it);
				shadowMemoryPHI->addIncoming(shadowMemory, *blockIt);
				++blockIt;
			}
		}
	}
}

void FullPass::CreateAndInsertFillMemoryCode(Value& ptr, Value& numBytes, Value& value, Instruction& insertBefore)
{
	//code to fill the newly allocated memory with 0's.
	BitCastInst* i8Ptr = new BitCastInst(&ptr, Type::getInt8PtrTy(*context), "", &insertBefore);
	MarkAsNotOriginal(*i8Ptr);
	std::vector<Value*> args;
	args.push_back(i8Ptr);
	args.push_back(&value);
	args.push_back(&numBytes);
	args.push_back(&GetInt(32, 1)); //@todo Putting the right alignment would be good
	args.push_back(&GetInt(1, 0));
	CallInst* call = CallInst::Create(&GetMemsetFunction(numBytes.getType()->getScalarSizeInBits()), args, "", &insertBefore); //@todo memory leak
	MarkAsNotOriginal(*call);
}


Value* FullPass::HandleExternFunctionCall(CallSite& cs)
{
	Function& func = *cs.getCalledFunction();
	
	if (func.getName().equals("printf"))
	{
		return HandlePrintfCall(cs);
	}
	else if (func.getName().equals("malloc"))
	{
		return HandleMallocCall(cs);
	}

	Type* retType = func.getReturnType();
	int size = targetData->getTypeSizeInBits(retType);
	assert(size != 0);

	if (retType->isVoidTy()) return 0; 
	
	if (retType->isPointerTy())
	{
		Constant& c = GetAllOnesValue(*retType);
		return &c;
	}
	else
	{
		return &GetNullValue(*retType); //I'm assuming that NullValue is 0
	}
}


Value* FullPass::HandlePrintfCall(CallSite& cs)
{
	CallSite::arg_iterator it = cs.arg_begin(), itEnd = cs.arg_end();
	it++;
	Function& assertZero = GetAssertZeroFunction();	
	for (; it != itEnd; ++it)
	{
		Value& shadow = GetShadow(*it->get());
		ICmpInst* cmp = new ICmpInst(cs.getInstruction(), CmpInst::ICMP_NE, &shadow, Constant::getNullValue(shadow.getType()), "");
		MarkAsNotOriginal(*cmp);
		std::vector<Value*> args;
		args.push_back(cmp);
		CallInst* assertZeroCall = CallInst::Create(&assertZero, args, "", cs.getInstruction());
		MarkAsNotOriginal(*assertZeroCall);
	}

	return &GetNullValue(*cs.getCalledFunction()->getReturnType());
}

Value* FullPass::HandleMallocCall(CallSite& cs)
{
	std::vector<Value*> args;
	args.push_back(cs.getArgument(0));
	
	std::vector<Type*> params;
	params.push_back(Type::getIntNTy(*context, target));
	
	Function* malloc = module->getFunction("malloc");
	assert(malloc != 0);
	CallInst* mallocShadow = CallInst::Create(malloc, args, "", cs.getInstruction());
	MarkAsNotOriginal(*mallocShadow);
	
	CreateAndInsertFillMemoryCode(*mallocShadow, *cs.getArgument(0), GetInt(8,0), *cs.getInstruction());
	return mallocShadow;
}

Instruction* FullPass::GetNextInstruction(Instruction& i)
{
	BasicBlock::iterator it(&i);
	it++;
	return it;
}

bool FullPass::IsExternalArg(Value& v)
{
	Argument* arg = dyn_cast<Argument>(&v);
	return arg && arg->getParent()->getName().equals("main") && (v.getName().equals("argc") || v.getName().equals("argv"));
}
