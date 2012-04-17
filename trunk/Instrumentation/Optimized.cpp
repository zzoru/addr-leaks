#include "Optimized.h"

#include <llvm/Support/raw_ostream.h>
#define DEBUG 
#ifndef DEBUG
#define db(x) ;;;
#else
#define db(x) errs() << x << "\n";
#endif


//@todo Remember to test phi with one path being clean and other being dirty

/*********************************************************************************************************************************************************************************
 * PUBLIC METHODS IMPLEMENTATION
 *********************************************************************************************************************************************************************************/
Optimized::Optimized() : ModulePass(ID)
{

}

bool Optimized::runOnModule(Module& module)
{
	Setup(module);
	HandleSpecialFunctions();
	
	leakedValues = analysis->getLeakedValues();

	
	for (std::set<Value*>::iterator it = leakedValues.begin(); it != leakedValues.end(); it++) {
		
		//TODO: remember to fix this in the future
		if (dyn_cast<Function>(*it))
		{
			
			continue;
		}
		
		HandleUses(**it);
	}
	
	
	
	return true;
}

/*********************************************************************************************************************************************************************************
 * PRIVATE METHODS IMPLEMENTATION
 *********************************************************************************************************************************************************************************/

//void Optimized::HandleStores(Value& v)
//{
//	for (Value::use_iterator it = v.use_begin(), end = v.use_end(); it != end; it++)
//	{
//		StoreInst* store = dyn_cast<StoreInst>(*it);
//		
//		if (store)
//		{	
//			if (store->getValueOperand() == &v)
//			{
//				Value& pointer = *store->getPointerOperand();
//				Value& shadowPointer = CreateTranslateCall(pointer, *store);
//				Value& shadow = GetShadow(v);
//				new StoreInst(&shadow, &shadowPointer, store); //TODO: memory leak?
//			}
//		}
//	}
//}

void Optimized::HandleUses(Value& v)
{
	Value& shadow = GetShadow(v);
	
	for (Value::use_iterator it = v.use_begin(), end = v.use_end(); it != end; it++)
	{
		//TODO: this may be an invoke or something like that
		CallInst* call = dyn_cast<CallInst>(*it);
		if (call) 
		{
			Function& f = *call->getCalledFunction();
			StringRef name = f.getName();
			
			if (name.equals("printf"))
			{
				ICmpInst* cmp = new ICmpInst(call, CmpInst::ICMP_NE, &shadow, Constant::getNullValue(shadow.getType()), "");
				Function& assertZero = GetAssertZeroFunction();
				std::vector<Value*> args;
				args.push_back(cmp);
				CallInst* assertZeroCall = CallInst::Create(&assertZero, args, "", call); //TODO: memory leak?
			}
//			else if (name.startswith("llvm.memcpy"))
//			{
//				Value* src = call->getOperand(0);
//				Value* dest = call->getOperand(1);
//				Value* len = call->getOperand(2);
//				Value* align = call->getOperand(3);
//				Value* isVolatile = call->getOperand(4);
//				
//				Value& newSrc = CreateTranslateCall(*src, *call);
//				Value& newDest = CreateTranslateCall(*dest, *call);
//				
//				std::vector<Value*> args;
//				args.push_back(&newSrc);
//				args.push_back(&newDest);
//				args.push_back(len);
//				args.push_back(align);
//				args.push_back(isVolatile);
//				new CallInst(&f, )
//			}
		}
		else 
		{
			//TODO: there may be more instructions that stores in memory
			StoreInst* store = dyn_cast<StoreInst>(*it);
			if (store)
			{
				
				if (store->getValueOperand() == &v)
				{
					Value& shadowPtr = CreateTranslateCall(*store->getPointerOperand(), *store);
					
					StoreInst* x = new StoreInst(&shadow, &shadowPtr, store); //TODO: memory leak?
					
					
				}
			}
		}
	}
}

void Optimized::HandleSpecialFunctions()
{
	for (Module::iterator fIt = module->begin(), fItEnd = module->end(); fIt != fItEnd; ++fIt) {
		if (! fIt->isDeclaration()) {
			for (inst_iterator it = inst_begin(fIt), itEnd = inst_end(fIt); it != itEnd; ++it)
			{
				MemCpyInst* mcy = dyn_cast<MemCpyInst>(&*it);
				if (mcy) {
					HandleMemcpy(*mcy);
				}
			}
		}
	}
}

void Optimized::HandleMemcpy(MemCpyInst& i)
{
	Function* f = i.getCalledFunction();
	Value* src = i.getRawSource();
	Value& newSrc = CreateTranslateCall(*src, i);
	Value* dest = i.getRawDest();
	Value& newDest = CreateTranslateCall(*dest, i);
	std::vector<Value*> args;
	args.push_back(&newSrc);
	args.push_back(&newDest);
	args.push_back(i.getLength()); //TODO: these args might be wrong
	args.push_back(i.getAlignmentCst());
	args.push_back(i.getVolatileCst());
	CallInst* c = CallInst::Create(f, args, "", &i);
	db("Original memcpy: " << i << "\nNew memcpy: " << *c << "\n")
}

//TODO: Look whether while loops (anything that depends on itself would work) would break this
//TODO: Remember to test null pointers
void Optimized::Setup(Module& module)
{
	analysis = &getAnalysis<AddrLeaks>();
	this->module = &module;
	this->context = &module.getContext();
	targetData = new TargetData(&module);
//	SetupParamPassingShadows();
//	
	Function& f = GetInitFunction();
	
//	//TODO: This may not be the best way to do it
	Function* main = module.getFunction("main");
	Instruction* first = main->getEntryBlock().getFirstNonPHI();
	CallInst* init = CallInst::Create(&f, "", first); //TODO: memory leak?

//	Function* main = module.getFunction("main");
//	Function::ArgumentListType& argList =  main->getArgumentList();
	
	//@todo this assumes that in main you either have 0 parameters or 2
//	if (argList.size() >= 2)
//	{
//		Argument& argc = argList.front();
//		Argument& argv = *argList.getNext(&argc);
//		Function& createArgvShadow = GetCreateArgvShadowFunction();
//		std::vector<Value*> args;
//		args.push_back(&argc);
//		args.push_back(&argv);
//		CallInst* callToCreateArgvShadow = CallInst::Create(&createArgvShadow, args, "", &*instructionIt);
//		AddShadowMemory(argv, *callToCreateArgvShadow);
//		AddShadowMemory(argc, GetNullValue(*Type::getInt32Ty(*context)));
//		MarkAsNotOriginal(*callToCreateArgvShadow);
//	}
}

Value& Optimized::Instrument(Value& value)
{
	
	Value* shadow;
	
	Instruction* i;
	GlobalVariable* gv;
	Argument* param;
	Constant* c;
	
	i = dyn_cast<Instruction>(&value);
	
	if (i)
	{
		shadow = &Instrument(*i);
		goto end;
	}
	
	gv = dyn_cast<GlobalVariable>(&value);
	
	if (gv)
	{	
		shadow = &GetAllOnesValue(*gv->getType());
		goto end;
	}

	param = dyn_cast<Argument>(&value);
	
	if (param)
	{
		//TODO: Maybe there are ways to discover whether the param is constant
		
		GlobalVariable* paramGV = new GlobalVariable(*module, param->getType(), false, GlobalValue::CommonLinkage, &GetNullValue(*param->getType()), "");  //TODO: not sure if this linkage is the right one
		
		Function* f = param->getParent();
		BasicBlock& b = f->getEntryBlock();
		Instruction* firstInstruction = b.getFirstNonPHI();
		shadow = new LoadInst(paramGV, "", firstInstruction);
		
		HandleParamPassingTo(*f, param->getArgNo(), paramGV);
		
		goto end;

//		Function& f = *param->getParent();
//		BasicBlock& entry = f.getBasicBlockList().front(); //TODO: not sure if it does what I expect it to do
////		GlobalVariable& gv = GetParamGlobal(*param);
//		shadow = new LoadInst(&gv, "", entry.getFirstNonPHI());
//		AddShadow(*param, *shadow); //TODO: It's done here rather than in "end" because of recursive functions
//		HandleParamPassingTo(*param->getParent());
//		goto end;
	}
	
	c = dyn_cast<Constant>(&value);
	
	if (c)
	{
		bool ptr = false;
		
		for (Constant::op_iterator it = c->op_begin(), end = c->op_end(); it != end; it++)
		{
			Value* v = *it;
			if (v->getType()->isPointerTy())
			{
				ptr = true;
				break;
			}
		}
		
		if (ptr)
		{
			shadow = &GetAllOnesValue(*value.getType());
		}
		else
		{
			shadow = &GetNullValue(*value.getType());
		}
		
		goto end;
	}
	
	if (value.getType()->isPointerTy())
	{
		shadow = &GetAllOnesValue(*value.getType());
		goto end;
	}
	
	
	
	
	
	assert(0 && "Shouldn't reach here");

//	std::map<Value*, Value*>::iterator it = valueToShadow.find(&value);
//
//	if (it != valueToShadow.end())
//	{
//		return *it->second;
//	}
//
//	Instruction* instruction = dyn_cast<Instruction>(&value);
//
//	if (instruction)
//	{
//		Value* shadow = Instrument(*instruction);
//		assert(shadow != 0);
//		return *shadow;
//	}

end:
	assert(shadow != 0);
	AddShadow(value, *shadow);
	
	MarkAsInstrumented(value);
	return *shadow;
}

Value& Optimized::Instrument(Instruction& instruction)
{	
	Value* newShadow = 0;
	
	//TODO: Make tests with type conversions including floats
	if (instruction.isBinaryOp())
	{
		BinaryOperator& bin = cast<BinaryOperator>(instruction);
		Value& op1 = *bin.getOperand(0);
		Value& op2 = *bin.getOperand(1);
		Value& shadow1 = GetShadow(op1);
		Value& shadow2 = GetShadow(op2);
		newShadow = BinaryOperator::Create(Instruction::Or, &shadow1, &shadow2, "", &instruction);
		return *newShadow;
	}

	switch (instruction.getOpcode())
	{
		//TODO: Handle vector operations
//		case Instruction::Ret:
//		{
//			assert(0 && 'This should never happen since this isntruction does not produce a value');
////			ReturnInst& ret = cast<ReturnInst>(instruction);
////			if (ret.getNumOperands() == 0) break;
////			
////			Value& op = *ret.getOperand(0);
////			Value& shadow = GetShadow(op);
////			GlobalVariable& returnShadow = GetReturnShadow(*shadow.getType());
////			StoreInst* store = new StoreInst(&shadow, &returnShadow, &instruction); 
////			MarkAsNotOriginal(*store);
//			break;
//		}
//		case Instruction::Invoke:
//		{
//			//TODO: I'll do this later
//			break;
//		}
//		case Instruction::Resume:
//		{
//			//TODO: I'll do this later
//			break;
//		}
		case Instruction::ExtractValue: //@todo Remember to test this
		{
			ExtractValueInst& extract = cast<ExtractValueInst>(instruction);
			Value* agg = extract.getAggregateOperand();
			Value& shadow = GetShadow(*agg);
			newShadow = ExtractValueInst::Create(&shadow, extract.getIndices(), "", &instruction);
			break;
		}
//		case Instruction::InsertValue: //@todo Remember to test this
//		{
//			assert(0 && 'This should never happen since this isntruction does not produce a value');
////			InsertValueInst& insert = cast<InsertValueInst>(instruction);
////			Value* agg =  insert.getAggregateOperand();
////			Value& aggShadow = GetShadow(*agg);
////			Value* insertedValue = insert.getInsertedValueOperand();
////			Value& insertedValueShadow = GetShadow(*insertedValue);
////			newShadow = InsertValueInst::Create(&aggShadow, &insertedValueShadow, insert.getIndices(), "", &instruction);
//			break;
//		}
		case Instruction::Alloca:
		{
			
//			//TODO: Add align info (I'm not sure of how relevant this is)
			AllocaInst& alloca = cast<AllocaInst>(instruction);
			newShadow = &GetAllOnesValue(*alloca.getType());
//			
//			newShadow = &GetAllOnesValue(*alloca.getType());
//			
//			//duplicating the alloca (creating the shadow memory)
//			AllocaInst* allocaClone = new AllocaInst(alloca.getAllocatedType(), alloca.getArraySize(), "", &instruction);
//			MarkAsNotOriginal(*allocaClone);
//			
////			int typeSizeInBytes = targetData->getTypeStoreSize(alloca.getAllocatedType());
////			Value* arraySize = alloca.getArraySize();
////			int arraySizeSizeInBits = targetData->getTypeSizeInBits(arraySize->getType());
////			BinaryOperator* mul = BinaryOperator::Create(Instruction::Mul, arraySize, &GetInt(arraySizeSizeInBits, typeSizeInBytes), "", &instruction);
////			MarkAsNotOriginal(*mul);
////			CreateAndInsertFillMemoryCode(*allocaClone, *mul, GetInt(8,0), instruction);
//			
//			AddShadowMemory(alloca, *allocaClone);
//			
//			//storing the relationship memory <---> shadow memory in the hash table
//			Function& addInHashFunction = GetAddInHashFunction();
//			std::vector<Value*> args;	
//			Instruction* nextInstruction = GetNextInstruction(instruction);
//			BitCastInst* cast = new BitCastInst(&alloca, Type::getInt8PtrTy(*context), "", nextInstruction);
//			MarkAsNotOriginal(*cast);
//			args.push_back(cast);
//			BitCastInst* castClone = new BitCastInst(allocaClone, Type::getInt8PtrTy(*context), "", nextInstruction);
//			MarkAsNotOriginal(*castClone);
//			args.push_back(castClone);
//			CallInst* call = CallInst::Create(&addInHashFunction, args, "", nextInstruction); 
//			MarkAsNotOriginal(*call);
			break;
		}
		case Instruction::Load:
		{
			LoadInst& load = cast<LoadInst>(instruction);
//			
			Value* pointer = load.getPointerOperand();
			Value& pointerShadowMemory = CreateTranslateCall(*pointer, load);
//			
			newShadow = new LoadInst(&pointerShadowMemory, "", &instruction);
//			
//			if (load.getType()->isPointerTy())
//			{
//				//getting from the hash the pointer to the shadow memory relative to the result of the load
//				Function& f = GetGetFromHashFunction();
//				std::vector<Value*> args;
//				Instruction* nextInstruction = GetNextInstruction(instruction);
//				BitCastInst* cast = new BitCastInst(&load, Type::getInt8PtrTy(*context), "", nextInstruction);
//				MarkAsNotOriginal(*cast);
//				args.push_back(cast);
//				CallInst* call = CallInst::Create(&f, args, "", nextInstruction);
//				MarkAsNotOriginal(*call);
//				BitCastInst* res = new BitCastInst(call, load.getType(), "", nextInstruction);
//				MarkAsNotOriginal(*res);
//				AddShadowMemory(load, *res);
//			}
			
			break;
		}
//		case Instruction::Store:
//		{
//			assert(0 && 'Should never be there since this instruction does not produce a value');
////			StoreInst& store = cast<StoreInst>(instruction);
////			Value* pointer = store.getPointerOperand();
////			Value& pointerShadow = GetShadowMemory(*pointer);
////			Value* storedValue = store.getValueOperand();
////			Value& storedValueShadow = GetShadow(*storedValue);
////			StoreInst* shadowStore = new StoreInst(&storedValueShadow, &pointerShadow, &instruction); 
////			MarkAsNotOriginal(*shadowStore);
//			break;
//		}
//		case Instruction::Fence:
//		{
//			//TODO: do later
//			break;
//		}
//		case Instruction::CMPXCHG:
//		{
//			//TODO: Do this later
//			break;
//		}
		//@todo atomic read write in memory
		case Instruction::GetElementPtr:
		{
			GetElementPtrInst& gep = cast<GetElementPtrInst>(instruction);
			newShadow = &GetAllOnesValue(*gep.getType());
			
//			Value* pointer = gep.getPointerOperand();
//			Value& shadow = GetShadowMemory(*pointer);
//			newShadow = new BitCastInst(&shadow, gep.getType(), "", &gep);
			
//			Value& pointerShadowMemory = GetShadowMemory(*pointer);
//			
//			std::vector<Value*> idx;
//
//			for (GetElementPtrInst::op_iterator it = gep.idx_begin(), itEnd = gep.idx_end(); it != itEnd; ++it)
//			{
//				idx.push_back(*it);
//			}
//			
//			GetElementPtrInst* shadowGep = GetElementPtrInst::Create(&pointerShadowMemory, idx, "", &instruction); //@todo Memory leak
//			MarkAsNotOriginal(*shadowGep);
//			AddShadowMemory(gep, *shadowGep);
//
//			newShadow = &GetAllOnesValue(*gep.getType());

			break;
		}
		//@todo conversion operators
		case Instruction::BitCast:
		{
			BitCastInst& bitcast = cast<BitCastInst>(instruction);
			Value& shadow = GetShadow(*bitcast.getOperand(0));
			newShadow = new BitCastInst(&shadow, bitcast.getDestTy(), "", &instruction);
			
//			if (bitcast.getOperand(0)->getType()->isPointerTy())
//			{
//				Value& shadowMemory = GetShadowMemory(*bitcast.getOperand(0));
//				BitCastInst* newShadowMemory = new BitCastInst(&shadowMemory, bitcast.getDestTy(), "", &instruction);
//				AddShadowMemory(bitcast, *newShadowMemory);
//				MarkAsNotOriginal(*newShadowMemory);
//			}
//			
			break;
		}
		case Instruction::ICmp:
		case Instruction::FCmp:
		{
			newShadow = &GetNullValue(*instruction.getType());
			break;
		}
		case Instruction::PHI:
		{
			PHINode& phi = cast<PHINode>(instruction);
			newShadow = PHINode::Create(phi.getType(), 0, "", &instruction);  //@todo I put NumReservedValues=0 because it's the safe choice and I don't know how to get the original one
			delayedPHINodes.insert(std::pair<PHINode*, PHINode*>(&phi, cast<PHINode>(newShadow)));
			
//			if (phi.getType()->isPointerTy())
//			{
//				PHINode* newMemoryShadow = PHINode::Create(phi.getType(), 0, "", &instruction); 
//				AddShadowMemory(phi, *newMemoryShadow);
//				delayedPHIShadowMemoryNodes.insert(std::pair<PHINode*, PHINode*>(&phi, cast<PHINode>(newMemoryShadow)));
//			}
			
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
			//TODO: remember to handle varargs
			
			CallInst& call = cast<CallInst>(instruction);
			Function& func = *call.getCalledFunction();
			
			

			if (! HasBody(func))
			{	
				
				CallSite cs(&call);
				newShadow = HandleExternFunctionCall(cs);
				break;
			}
			else
			{	
				HandleReturns(func);
//				Function::ArgumentListType& paramList = func.getArgumentList();
//				Function::ArgumentListType::iterator it = paramList.begin();
//				
//				for (int i = 0; i < call.getNumArgOperands(); i++, paramList++)
//				{
//					Value* arg = call.getArgOperand(i);
//					Value& argShadow = GetShadow(*arg);
//					Argument& param = *paramList;
//					GlobalVariable& paramGlobal = GetParamGlobal(param);
//					StoreInst* store = new StoreInst(&argShadow, &paramGlobal, &instruction); //@todo memory leak
//				}
//	
				Instruction* nextInstruction = GetNextInstruction(instruction);
				Value& returnShadow = GetReturnGlobal(func);
				newShadow = new LoadInst(&returnShadow, "", nextInstruction); 
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
			Value& shadow = GetShadow(*conv.getOperand(0));
			newShadow = new IntToPtrInst(&shadow, conv.getDestTy(), "", &instruction);
			break;
		}
		case Instruction::PtrToInt: 
		{
			PtrToIntInst& conv = cast<PtrToIntInst>(instruction);
			Value& shadow = GetShadow(*conv.getOperand(0));
			newShadow = new PtrToIntInst(&shadow, conv.getDestTy(), "", &instruction);
			break;
		} 
		//TODO: va_arg
		//TODO: landingpad
		default:
		{
			
			break;
		}
	}
	
end:
	assert(newShadow != 0);
//	if (newShadow != 0)
//	{
//		valueToShadow.insert(std::pair<Value*, Value*>(&instruction, newShadow));
//		Instruction* i = dynamic_cast<Instruction*>(newShadow);
//
//		if (i)
//		{
//			MarkAsNotOriginal(*i);
//		}
//	}

	
	return *newShadow;
}

Function& Optimized::GetAssertZeroFunction()
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

	BasicBlock* ifTrue = BasicBlock::Create(*context, "", assertZero);
	ReturnInst* ret = ReturnInst::Create(*context, ifTrue);
	
	BasicBlock* ifFalse = BasicBlock::Create(*context, "", assertZero);

	CallInst* call = CallInst::Create(quitProgram, "", ifFalse);

	UnreachableInst* unr = new UnreachableInst(*context, ifFalse);
	
	BranchInst* branch = BranchInst::Create(ifTrue, ifFalse, cmp, entry);
	
	return GetAssertZeroFunction();
}

Function& Optimized::GetTranslateFunction()
{
	static Function* func = 0;
	
	if (func) return *func;
	
	std::vector<Type*> args;
	args.push_back(Type::getInt8PtrTy(*context));
	func = Function::Create(FunctionType::get(Type::getInt8PtrTy(*context), args, false), GlobalValue::ExternalLinkage, "translate", module);
	return *func;
}

Function& Optimized::GetInitFunction()
{
	static Function* func = 0;

	if (func) return *func;

	func = Function::Create(FunctionType::get(Type::getVoidTy(*context), false), GlobalValue::ExternalLinkage, "createShadowMemory", module);
	return *func;
}
	
Constant& Optimized::GetInt(int width, int value)
{
	ConstantInt& constant = *ConstantInt::get(Type::getIntNTy(*context, width), value, true);
	return constant;
}

Constant& Optimized::GetAllOnesValue(Type& type)
{
	int numBits = targetData->getTypeSizeInBits(&type);
	uint64_t val = 0;
	APInt ap(numBits, ~val, false);
	return *Constant::getIntegerValue(&type, ap);
}

Constant& Optimized::GetNullValue(Type& type)
{
	int numBits = targetData->getTypeSizeInBits(&type);
	uint64_t val = 0;
	APInt ap(numBits, val, false);
	return *Constant::getIntegerValue(&type, ap);
}

//ConstantInt& Optimized::Get1FilledInt(int width)
//{
//	return GetInt(width, pow(2, width) - 1);
//}

bool Optimized::HasBody(Function& f)
{
	
	return f.getBasicBlockList().size() != 0;
}

void Optimized::HandleReturns(Function& f)
{
	
	
	//TODO: remember to handle things like invoke vs call
	static std::set<Function*> alreadyHandled;
	
	std::set<Function*>::iterator it = alreadyHandled.find(&f);
	
	if (it != alreadyHandled.end()) return;
	
	
	GlobalVariable& returnGlobal = GetReturnGlobal(f);
	
	
	
	//TODO: this is really ugly. there has to be another way 
	for (inst_iterator i = inst_begin(f), ie = inst_end(f); i != ie; ++i)
	{
		ReturnInst* ret = dyn_cast<ReturnInst>(&*i);
		
		if (ret) {
			assert(ret->getNumOperands() >= 1);
			Value& shadow = GetShadow(*ret->getOperand(0));
			
//			StoreInst* store = new StoreInst(&shadow, &returnGlobal, &ret); //TODO: memory leak?
			StoreInst* store = new StoreInst(&shadow, &returnGlobal, ret);
			
			
		}
	}
	
	
	alreadyHandled.insert(&f);
}

//void Optimized::HandleParam(Argument& arg)
//{
//	
//}

GlobalVariable& Optimized::GetReturnGlobal(Function& func)
{
	static std::map<Function*, GlobalVariable*> returnShadows;
	
	std::map<Function*, GlobalVariable*>::iterator it = returnShadows.find(&func);
	
	if (it == returnShadows.end())
	{
		GlobalVariable* returnShadow = new GlobalVariable(*module, func.getReturnType(), false, GlobalValue::CommonLinkage, &GetNullValue(*func.getReturnType()), "");
		returnShadows.insert(std::pair<Function*, GlobalVariable*>(&func, returnShadow));
		return *returnShadow;
	}
	
	return *it->second;
}

//GlobalVariable& Optimized::GetReturnShadowMemory(Type& type)
//{
//	static std::map<Type*, GlobalVariable*> returnShadows;
//
//	std::map<Type*, GlobalVariable*>::iterator it = returnShadows.find(&type);
//	GlobalVariable* returnShadow;
//
//	if (it == returnShadows.end())
//	{
//		returnShadow = new GlobalVariable(*module, &type, false, GlobalValue::CommonLinkage, &GetNullValue(type), "");
//		returnShadows.insert(std::pair<Type*, GlobalVariable*>(&type, returnShadow));
//	}
//	else
//	{
//		returnShadow = it->second;
//	}
//
//	return *returnShadow;
//}

void Optimized::AddShadow(Value& value, Value& shadow)
{
	valueToShadow.insert(std::pair<Value*, Value*>(&value, &shadow));
}

Value& Optimized::GetShadow(Value& value)
{
	//TODO: I'm adding to valueToShadow in Instrument. Whether this is a good idea remains to be seen
	std::map<Value*, Value*>::iterator it = valueToShadow.find(&value);
	
	if (it != valueToShadow.end()) return *it->second;
	
	if (! MayBePointer(value))
	{
		return GetNullValue(*value.getType());
	}
	
	Value& shadow = Instrument(value);
	return shadow;
	
//	GlobalVariable* gv = dyn_cast<GlobalVariable>(&value);
//
//	if (gv)
//	{
//		return GetAllOnesValue(*gv->getType());
//	}
//
//	Constant* constant = dyn_cast<Constant>(&value);
//
//	if (constant)
//	{
//		return GetNullValue(*constant->getType());
//	}
//	
//	Argument* argument = dyn_cast<Argument>(&value);
//	
//	if (argument && argument->getParent()->getName().equals("main"))
//	{
//		if (argument->getType()->getScalarSizeInBits() == 32)
//		{
//			return GetInt(32,0);
//		}
//		else
//		{
//			return GetAllOnesValue(*argument->getType());
//		}
//	}
//	
//	std::map<Value*, Value*>::iterator it = valueToShadow.find(&value);
//	
//	if (it != valueToShadow.end())
//	{
//		return *it->second;
//	}
//	
//	Instruction* instruction = dyn_cast<Instruction>(&value);
//	
//	if (instruction)
//	{
//		Value* shadow = Instrument(*instruction);
//		assert(shadow != 0);
//		return *shadow;
//	}
//	
//	assert(0);
//	return value; //@todo remove this
}

Value& Optimized::CreateTranslateCall(Value& pointer, Instruction& before)
{
	
	Function& translateFunction = GetTranslateFunction();
	
	//first I must bitcast pointer to i8* since the translate function expects a i8* 
	BitCastInst* toi8p = new BitCastInst(&pointer, Type::getInt8PtrTy(*context), "", &before);
	
	std::vector<Value*> args;
	args.push_back(toi8p);
	CallInst* call = CallInst::Create(&translateFunction, args, "", &before);
	
	BitCastInst* fromi8p = new BitCastInst(call, pointer.getType(), "", &before);
	
	
	return *fromi8p;
}

//@todo If there's an infinite loop then this is a probable cause
//Value& Optimized::GetShadowMemory(Value& value)
//{
//	std::map<Value*, Value*> shadowMemoryMap;
//	std::map<Value*, Value*>::iterator it = shadowMemoryMap.find(&value);
//	
//	if (it != shadowMemoryMap.end())
//	{
//		return *it->second;
//	}
//	
//	Value* newShadowMemory = 0;
//	
//	//TODO: these where declared here because otherwise goto won't work
//	Instruction* i;
//	GlobalVariable* gv;
//	
//	gv = dyn_cast<GlobalVariable>(&value);
//	if (gv)
//	{
//		newShadowMemory = new GlobalVariable(*module, gv->getType(), gv->isConstant(), gv->getLinkage(), 
//				                                   &GetNullValue(*gv->getType()->getElementType()), "", gv, gv->isThreadLocal()); 
//		goto end;
//	}
//	
//	i = dyn_cast<Instruction>(&value);
//	assert(i != 0);
//
//	switch (i->getOpcode())
//	{
//		//@todo Handle vector operations
//		//		case Instruction::Ret:
//		//		{
//		//			assert(0 && 'This should never happen since this isntruction does not produce a value');
//		////			ReturnInst& ret = cast<ReturnInst>(instruction);
//		////			if (ret.getNumOperands() == 0) break;
//		////			
//		////			Value& op = *ret.getOperand(0);
//		////			Value& shadow = GetShadow(op);
//		////			GlobalVariable& returnShadow = GetReturnShadow(*shadow.getType());
//		////			StoreInst* store = new StoreInst(&shadow, &returnShadow, &instruction); 
//		////			MarkAsNotOriginal(*store);
//		//			break;
//		//		}
//		//		case Instruction::Invoke:
//		//		{
//		//			//@todo I'll do this later
//		//			break;
//		//		}
//		//		case Instruction::Resume:
//		//		{
//		//			//@todo I'll do this later
//		//			break;
//		//		}
//	//TODO: later
//		case Instruction::ExtractValue: //@todo Remember to test this
//		{
//			ExtractValueInst& extract = cast<ExtractValueInst>(*i);
//			Value* agg = extract.getAggregateOperand();
//			Value& shadowMemory = GetShadowMemory(*agg);
//			newShadowMemory = ExtractValueInst::Create(&shadowMemory, extract.getIndices(), "", i);
//			break;
//		}
//		//		case Instruction::InsertValue: //@todo Remember to test this
//		//		{
//		//			assert(0 && 'This should never happen since this isntruction does not produce a value');
//		////			InsertValueInst& insert = cast<InsertValueInst>(instruction);
//		////			Value* agg =  insert.getAggregateOperand();
//		////			Value& aggShadow = GetShadow(*agg);
//		////			Value* insertedValue = insert.getInsertedValueOperand();
//		////			Value& insertedValueShadow = GetShadow(*insertedValue);
//		////			newShadow = InsertValueInst::Create(&aggShadow, &insertedValueShadow, insert.getIndices(), "", &instruction);
//		//			break;
//		//		}
//	//TODO: need to think harder about this
//		case Instruction::Alloca:
//		{
//			//			int typeSizeInBytes = targetData->getTypeStoreSize(alloca.getAllocatedType());
//			//			Value* arraySize = alloca.getArraySize();
//			//			int arraySizeSizeInBits = targetData->getTypeSizeInBits(arraySize->getType());
//			//			BinaryOperator* mul = BinaryOperator::Create(Instruction::Mul, arraySize, &GetInt(arraySizeSizeInBits, typeSizeInBytes), "", &instruction);
//			//			MarkAsNotOriginal(*mul);
//			//			CreateAndInsertFillMemoryCode(*allocaClone, *mul, GetInt(8,0), instruction);
//
//			//storing the relationship memory <---> shadow memory in the hash table
//			break;
//		}
//		case Instruction::Load:
//		{
//			LoadInst& load = cast<LoadInst>(*i);		
//			Value* pointer = load.getPointerOperand();
//			newShadowMemory = CreateTranslateCall(pointer, GetNextInstruction(load));
//			
////			Value& pointerShadowMemory = GetShadowMemory(*pointer);
////			//			
////			newShadow = new LoadInst(&pointerShadowMemory, "", &instruction);
//			//			
//			//			if (load.getType()->isPointerTy())
//			//			{
//			//				//getting from the hash the pointer to the shadow memory relative to the result of the load
//			//				Function& f = GetGetFromHashFunction();
//			//				std::vector<Value*> args;
//			//				Instruction* nextInstruction = GetNextInstruction(instruction);
//			//				BitCastInst* cast = new BitCastInst(&load, Type::getInt8PtrTy(*context), "", nextInstruction);
//			//				MarkAsNotOriginal(*cast);
//			//				args.push_back(cast);
//			//				CallInst* call = CallInst::Create(&f, args, "", nextInstruction);
//			//				MarkAsNotOriginal(*call);
//			//				BitCastInst* res = new BitCastInst(call, load.getType(), "", nextInstruction);
//			//				MarkAsNotOriginal(*res);
//			//				AddShadowMemory(load, *res);
//			//			}
//
//			break;
//		}
//		//		case Instruction::Store:
//		//		{
//		//			assert(0 && 'Should never be there since this instruction does not produce a value');
//		////			StoreInst& store = cast<StoreInst>(instruction);
//		////			Value* pointer = store.getPointerOperand();
//		////			Value& pointerShadow = GetShadowMemory(*pointer);
//		////			Value* storedValue = store.getValueOperand();
//		////			Value& storedValueShadow = GetShadow(*storedValue);
//		////			StoreInst* shadowStore = new StoreInst(&storedValueShadow, &pointerShadow, &instruction); 
//		////			MarkAsNotOriginal(*shadowStore);
//		//			break;
//		//		}
//		//		case Instruction::Fence:
//		//		{
//		//			//@todo do later
//		//			break;
//		//		}
//		//		case Instruction::CMPXCHG:
//		//		{
//		//			@todo Do this later
//		//			break;
//		//		}
//		//@todo atomic read write in memory
//		case Instruction::GetElementPtr:
//		{
//			GetElementPtrInst& gep = cast<GetElementPtrInst>(*i);
//			Value* pointer = gep.getPointerOperand();
//			newShadowMemory = CreateTranslateCall(pointer, GetNextInstruction(gep));
//
//			//			Value& pointerShadowMemory = GetShadowMemory(*pointer);
//			//			
//			//			std::vector<Value*> idx;
//			//
//			//			for (GetElementPtrInst::op_iterator it = gep.idx_begin(), itEnd = gep.idx_end(); it != itEnd; ++it)
//			//			{
//			//				idx.push_back(*it);
//			//			}
//			//			
//			//			GetElementPtrInst* shadowGep = GetElementPtrInst::Create(&pointerShadowMemory, idx, "", &instruction); //@todo Memory leak
//			//			MarkAsNotOriginal(*shadowGep);
//			//			AddShadowMemory(gep, *shadowGep);
//			//
//			//			newShadow = &GetAllOnesValue(*gep.getType());
//
//			break;
//		}
//		//@todo conversion operators
//		case Instruction::BitCast:
//		{
//			BitCastInst& bitcast = cast<BitCastInst>(*i);
//			Value& shadow = GetShadowMemory(*bitcast.getOperand(0));
//			newShadowMemory = new BitCastInst(&shadow, bitcast.getDestTy(), "", i);
//
//			//			if (bitcast.getOperand(0)->getType()->isPointerTy())
//			//			{
//			//				Value& shadowMemory = GetShadowMemory(*bitcast.getOperand(0));
//			//				BitCastInst* newShadowMemory = new BitCastInst(&shadowMemory, bitcast.getDestTy(), "", &instruction);
//			//				AddShadowMemory(bitcast, *newShadowMemory);
//			//				MarkAsNotOriginal(*newShadowMemory);
//			//			}
//			//			
//			break;
//		}
////		case Instruction::ICmp:
////		case Instruction::FCmp:
////		{
////			newShadow = &GetNullValue(*instruction.getType());
////			break;
////		}
//		case Instruction::PHI:
//		{
//			PHINode& phi = cast<PHINode>(*i);
//			newShadowMemory = PHINode::Create(phi.getType(), 0, "", i);  //@todo I put NumReservedValues=0 because it's the safe choice and I don't know how to get the original one
//			delayedPHIShadowMemoryNodes.insert(std::pair<PHINode*, PHINode*>(&phi, cast<PHINode>(newShadowMemory)));
//
//			//			if (phi.getType()->isPointerTy())
//			//			{
//			//				PHINode* newMemoryShadow = PHINode::Create(phi.getType(), 0, "", &instruction); 
//			//				AddShadowMemory(phi, *newMemoryShadow);
//			//				delayedPHIShadowMemoryNodes.insert(std::pair<PHINode*, PHINode*>(&phi, cast<PHINode>(newMemoryShadow)));
//			//			}
//
//			break;
//		}
//		case Instruction::Select:
//		{
//			SelectInst& select = cast<SelectInst>(*i);
//			Value& trueCondValueShadow = GetShadow(*select.getTrueValue());
//			Value& falseCondValueShadow = GetShadow(*select.getFalseValue());
//			newShadowMemory = SelectInst::Create(select.getCondition(), &trueCondValueShadow, &falseCondValueShadow, "", i);
//			break;
//		}
//		case Instruction::Call:
//		{
//			//TODO: remember to handle varargs
//
//			CallInst& call = cast<CallInst>(*i);
//			Function& func = *call.getCalledFunction();
//
//
//			if (! HasBody(func))
//			{	
//				CallSite cs(&call);
//				newShadowMemory = HandleExternFunctionCall(cs);
//				break;
//			}
//			else
//			{	
//				HandleReturns(func);
//				//				Function::ArgumentListType& paramList = func.getArgumentList();
//				//				Function::ArgumentListType::iterator it = paramList.begin();
//				//				
//				//				for (int i = 0; i < call.getNumArgOperands(); i++, paramList++)
//				//				{
//				//					Value* arg = call.getArgOperand(i);
//				//					Value& argShadow = GetShadow(*arg);
//				//					Argument& param = *paramList;
//				//					GlobalVariable& paramGlobal = GetParamGlobal(param);
//				//					StoreInst* store = new StoreInst(&argShadow, &paramGlobal, &instruction); //@todo memory leak
//				//				}
//				//	
//				Instruction* nextInstruction = GetNextInstruction(*i); //TODO: this may not be a good idea
//				Value& returnShadow = GetReturnGlobal(func);
//				newShadowMemory = new LoadInst(&returnShadow, "", nextInstruction); 
//			}
//
//
//			break;
//		}
//		case Instruction::Trunc:    
//		{
//			TruncInst& trunc = cast<TruncInst>(*i);
//			Value& shadow = GetShadow(*trunc.getOperand(0));
//			newShadowMemory = new TruncInst(&shadow, trunc.getDestTy(), "", i);
//			break;
//		}
//		case Instruction::ZExt:     
//		{
//			ZExtInst& zext = cast<ZExtInst>(*i);
//			Value& shadow = GetShadow(*zext.getOperand(0));
//			newShadowMemory = new ZExtInst(&shadow, zext.getDestTy(), "", i);
//			break;
//		}
//		case Instruction::SExt:      
//		{
//			SExtInst& sext = cast<SExtInst>(*i);
//			Value& shadow = GetShadow(*sext.getOperand(0));
//			newShadowMemory = new SExtInst(&shadow, sext.getDestTy(), "", i);
//			break;
//		}
//		//@todo Do those later
//		//		case Instruction::FPTrunc:   
//		//		case Instruction::FPExt:     
//		//		case Instruction::FPToUI:    
//		//		case Instruction::FPToSI:   
//		//		case Instruction::UIToFP:    
//		case Instruction::SIToFP:   
//		{
//			SIToFPInst& conv = cast<SIToFPInst>(*i);
//			newShadowMemory = new BitCastInst(conv.getOperand(0), conv.getDestTy(), "", i);
//			break;
//		}
//		case Instruction::IntToPtr:  
//		{
//			IntToPtrInst& conv = cast<IntToPtrInst>(*i);
//			newShadowMemory = new IntToPtrInst(conv.getOperand(0), conv.getDestTy(), "", i);
//			break;
//		}
//		case Instruction::PtrToInt: 
//		{
//			PtrToIntInst& conv = cast<PtrToIntInst>(*i);
//			newShadowMemory = new PtrToIntInst(conv.getOperand(0), conv.getDestTy(), "", i);
//			break;
//		} 
//		//@todo va_arg
//		//@todo landingpad
//		default:
//		{
//			break;
//		}
//	}
//
//	//	
//	//	Instruction* instruction = dyn_cast<Instruction>(&value);
//	//
//	//	if (instruction)
//	//	{
//	//		Instrument(*instruction);
//	//		return GetShadowMemory(value);
//	//	}
//	//	
//	//	assert(0 && "this should never be reached. value is probably a constant expression");
//end:
//	assert(newShadowMemory != 0);
//	shadowMemoryMap.insert(std::pair<Value*, Value*>(&value, newShadowMemory));
//	
//	return *newShadowMemory;
//}

//void Optimized::AddShadowMemory(Value& original, Value& shadow)
//{
//	shadowMemoryMap.insert(std::pair<Value*, Value*>(&original, &shadow));
//}

//void Optimized::SetupParamPassingShadows()
//{
//	for (Module::iterator it = module->begin(), itEnd = module->end(); it != itEnd; ++it)
//	{
//		Function& func = *it;
//		
//		if (func.getBasicBlockList().size() == 0 || func.getName().equals("main")) continue;
//		
//		inst_iterator firstInst = inst_begin(&func);
//		
//		Function::ArgumentListType& params = func.getArgumentList();
//		std::vector<GlobalVariable*> vec;
//	
//		
//		for (Function::ArgumentListType::iterator paramIt = params.begin(), paramItEnd = params.end(); paramIt != paramItEnd; ++paramIt)
//		{
//			GlobalVariable* gv = new GlobalVariable(*module, paramIt->getType(), false, GlobalValue::CommonLinkage, &GetNullValue(*paramIt->getType()), "");
//			vec.push_back(gv);
//			LoadInst* argShadow = new LoadInst(gv, "", &*firstInst); //@todo memory leak? 
//			                                   //@todo This &* looks horrible but seems to be necessary. Perhaps there is a better way
//			MarkAsNotOriginal(*argShadow);
//			valueToShadow.insert(std::pair<Value*, Value*>(paramIt, argShadow));
//		}
//		
//		paramsShadows.insert(std::pair<Function*, std::vector<GlobalVariable*> >(&func, vec));
//	}
//}

//GlobalVariable& Optimized::GetParamGlobal(Argument& param)
//{
//	static std::map<Argument*, GlobalVariable* > shadows;
//	std::map<Argument*, GlobalVariable*>::iterator it = shadows.find(&param);
//
//	if (it == shadows.end())
//	{
//		Function* f = param.getParent();
//		BasicBlock& entry = f->getBasicBlockList().front();
//		GlobalVariable* gv = new GlobalVariable(*module, param.getType(), false, GlobalValue::CommonLinkage, &GetNullValue(*param.getType()), "");
//		shadows.insert(std::pair<Argument*, GlobalVariable*>(&param, gv));
//		return *gv;
//	}
//
//	return *it->second;
//}

//void Optimized::AddParamShadow(Argument& arg, GlobalVariable& gv)
//{
//	
//}

Function& Optimized::GetMemsetFunction(int bitSize)
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

void Optimized::InstrumentDelayedPHINodes()
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
		
	}
	
//	for (std::map<PHINode*, PHINode*>::iterator it = delayedPHIShadowMemoryNodes.begin(), itEnd = delayedPHIShadowMemoryNodes.end(); it != itEnd; ++it)
//	{
//		PHINode& original = *it->first;
//		PHINode& phiShadow = *it->second;
//
//		PHINode::block_iterator blockIt = original.block_begin(), blockItEnd = original.block_end();
//		for (PHINode::op_iterator it = original.op_begin(), itEnd = original.op_end(); it != itEnd; ++it)
//		{
//			Value& shadow = GetShadowMemory(**it);
//			phiShadow.addIncoming(&shadow, *blockIt);
//			++blockIt;
//		}
//
//	}
}

//void Optimized::CreateAndInsertFillMemoryCode(Value& ptr, Value& numBytes, Value& value, Instruction& insertBefore)
//{
//	//code to fill the newly allocated memory with 0's.
//	BitCastInst* i8Ptr = new BitCastInst(&ptr, Type::getInt8PtrTy(*context), "", &insertBefore);
////	MarkAsNotOriginal(*i8Ptr);
//	std::vector<Value*> args;
//	args.push_back(i8Ptr);
//	args.push_back(&value);
//	args.push_back(&numBytes);
//	args.push_back(&GetInt(32, 1)); //@todo Putting the right alignment would be good
//	args.push_back(&GetInt(1, 0));
//	CallInst* call = CallInst::Create(&GetMemsetFunction(numBytes.getType()->getScalarSizeInBits()), args, "", &insertBefore); //@todo memory leak
////	MarkAsNotOriginal(*call);
//}

void Optimized::HandleParamPassingTo(Function& f, int argno, GlobalVariable* gv)
{
	for (Value::use_iterator it = f.use_begin()	, itEnd = f.use_end(); it != itEnd; it++)
	{
		//TODO: remember to handle invoke as well
		CallInst* call = dyn_cast<CallInst>(*it);

		if (! call)
		{
			assert(0 && "Forgot to test something");
		}

		CallSite cs(call);
		Value* arg = cs.getArgument(argno);
		Value& shadow = GetShadow(*arg);
		StoreInst* store = new StoreInst(&shadow, gv, cs.getInstruction()); //TODO: memory leak?
		
	}
	
	
//	static std::set<Function*> alreadyHandled;
//	
//	std::set<Function*>::iterator it = alreadyHandled.find(&f);
//	
//	if (it != alreadyHandled.end()) return;
//	
//	//TODO: remember varargs, invoke
//	std::vector<GlobalVariable*> globals;
//	
//	Function::ArgumentListType& paramList = f.getArgumentList();
//	
//	for (Function::ArgumentListType::iterator it = paramList.begin(), itEnd = paramList.end(); it != itEnd; it++)
//	{
//		bool p = MayBePointer(*it);
//		
//		if (p)
//		{
//			GlobalVariable& gv = GetParamGlobal(*it);
//			globals.push_back(&gv);
//		}
//		else
//		{
//			globals.push_back(0);
//		}
//	}
//	
//	for (Value::use_iterator it = f.use_begin()	, itEnd = f.use_end(); it != itEnd; it++)
//	{
//		//TODO: remember to handle invoke as well
//		CallInst* call = dyn_cast<CallInst>(*it);
//
//		if (! call)
//		{
//			assert(0 && "Forgot to test something");
//		}
//
//		CallSite cs(call);
//		std::vector<GlobalVariable*>::iterator globalsIt = globals.begin();
//		
//		for (CallSite::arg_iterator it = cs.arg_begin(), itEnd = cs.arg_end(); it != itEnd; it++)
//		{
//			if (*globalsIt != 0)
//			{
//				new StoreInst(*it, *globalsIt, cs.getInstruction()); //TODO: memory leak?
//			}
//			
//			globalsIt++;
//		}
//
//	}
//
//	alreadyHandled.insert(&f);
}


Value* Optimized::HandleExternFunctionCall(CallSite& cs)
{
	Function& func = *cs.getCalledFunction();
	
//	if (func.getName().equals("printf"))
//	{
//		return HandlePrintfCall(cs);
//	}
//	if (func.getName().equals("malloc"))
//	{
//		return HandleMallocCall(cs);
//	}

	
	
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


//Value* Optimized::HandlePrintfCall(CallSite& cs)
//{
//	CallSite::arg_iterator it = cs.arg_begin(), itEnd = cs.arg_end();
//	it++;
//	Function& assertZero = GetAssertZeroFunction();	
//	for (; it != itEnd; ++it)
//	{
//		Value& shadow = GetShadow(*it->get());
//		ICmpInst* cmp = new ICmpInst(cs.getInstruction(), CmpInst::ICMP_NE, &shadow, Constant::getNullValue(shadow.getType()), "");
////		MarkAsNotOriginal(*cmp);
//		std::vector<Value*> args;
//		args.push_back(cmp);
//		CallInst* assertZeroCall = CallInst::Create(&assertZero, args, "", cs.getInstruction());
////		MarkAsNotOriginal(*assertZeroCall);
//	}
//
//	return &GetNullValue(*cs.getCalledFunction()->getReturnType());
//}

//Value* Optimized::HandleMallocCall(CallSite& cs)
//{
//	
//	Instruction* mallocCall = cs.getInstruction();
//	return &GetAllOnesValue(*mallocCall->getType());
////	std::vector<Value*> shadowMallocCallArgs;
////	shadowMallocCallArgs.push_back(cs.getArgument(0));
////	
////	std::vector<Type*> params;
////	params.push_back(Type::getIntNTy(*context, target));
////	
////	Function* malloc = module->getFunction("malloc");
////	assert(malloc != 0);
////	CallInst* mallocShadow = CallInst::Create(malloc, shadowMallocCallArgs, "", mallocCall);
//////	MarkAsNotOriginal(*mallocShadow);
////	AddShadowMemory(*mallocCall, *mallocShadow);
////
////	//storing the relationship memory <---> shadow memory in the hash table
////	Function& addInHashFunction = GetAddInHashFunction();
////	std::vector<Value*> args;	
////	Instruction* nextInstruction = GetNextInstruction(*mallocCall);
////	BitCastInst* cast = new BitCastInst(mallocCall, Type::getInt8PtrTy(*context), "", nextInstruction);
//////	MarkAsNotOriginal(*cast);
////	args.push_back(cast);
////	BitCastInst* castClone = new BitCastInst(mallocShadow, Type::getInt8PtrTy(*context), "", nextInstruction);
//////	MarkAsNotOriginal(*castClone);
////	args.push_back(castClone);
////	CallInst* call = CallInst::Create(&addInHashFunction, args, "", nextInstruction); 
//////	MarkAsNotOriginal(*call);
////	
////	
////	
////	
//////	CreateAndInsertFillMemoryCode(*mallocShadow, *cs.getArgument(0), GetInt(8,0), *cs.getInstruction());
////	return mallocShadow;
//}

Instruction* Optimized::GetNextInstruction(Instruction& i)
{
	//TODO: This may not make sense since a instruction might have more than one sucessor or be the last instruction
	BasicBlock::iterator it(&i);
	it++;
	return it;
}

Function& Optimized::GetCreateArgvShadowFunction()
{
	static Function* func = 0;

	if (func) return *func;

	std::vector<Type*> params;
	params.push_back(Type::getInt32Ty(*context)); 
	params.push_back(Type::getInt8PtrTy(*context)->getPointerTo());
	func = Function::Create(FunctionType::get(Type::getInt8PtrTy(*context)->getPointerTo(), params, false), GlobalValue::ExternalLinkage, "createArgvShadow", module);
	return *func;
}

void Optimized::getAnalysisUsage(AnalysisUsage &info) const
{
	info.addRequired<AddrLeaks>();
}

bool Optimized::AlreadyInstrumented(Value& v)
{
	std::set<Value*>::iterator it = instrumented.find(&v);
	return it != instrumented.end();
}

void Optimized::MarkAsInstrumented(Value& v)
{
	instrumented.insert(&v);
}

bool Optimized::MayBePointer(Value& v)
{
	std::set<Value*>::iterator it = leakedValues.find(&v);
	return it != leakedValues.end();
}

//Value* Optimized::CreateGetFromHashCall(Value* pointer, Instruction* after)
//{
//	Function& f = GetGetFromHashFunction();
//	Value* p = new BitCastInst(pointer, Type::getInt8PtrTy(*context), "", after);
//	std::vector<Value*> arguments;
//	arguments.push_back(p);
//	CallInst* call = new CallInst(&f, arguments, "", after); //TODO: memory leak?
//	return new BitCastInst(call, pointer->getType(), "", after); //TODO: memory leak?
//}
