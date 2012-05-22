#include "Optimized.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

#define DEBUG 
#ifndef DEBUG
#define db(x) ;;;
#else
#define db(x) errs() << x << "\n";
#endif

cl::opt<bool> IsDumb("d", cl::desc("Specify whether to use the static analysis"), cl::desc("is dumb"));

//@todo Remember to test phi with one path being clean and other being dirty

/*********************************************************************************************************************************************************************************
 * PUBLIC METHODS IMPLEMENTATION
 *********************************************************************************************************************************************************************************/
Optimized::Optimized() : ModulePass(ID)
{

}

bool Optimized::runOnModule(Module& module)
{
	db("*********************************\nIn my pass*********************************")
																													dumb = IsDumb;
	Setup(module);
	HandleSpecialFunctions();

	if (dumb)
	{
		for (Module::iterator funcIt = module.begin(), funcItEnd = module.end(); funcIt != funcItEnd; funcIt++)
		{
			for (inst_iterator it = inst_begin(*funcIt), itEnd = inst_end(*funcIt); it != itEnd; it++)
			{
				if (! AlreadyInstrumented(*it))
				{

					HandleUses(*it);
				}

			}
		}
	}
	else
	{
		leakedValues = analysis->getLeakedValues();


		for (std::set<Value*>::iterator it = leakedValues.begin(); it != leakedValues.end(); it++) {

			//TODO: remember to fix this in the future
			if (dyn_cast<Function>(*it))
			{

				continue;
			}

			HandleUses(**it);
		}
	}

	InstrumentDelayedPHINodes();
	db("Cheguei aqui*********************");

	return true;
}

/*********************************************************************************************************************************************************************************
 * PRIVATE METHODS IMPLEMENTATION
 *********************************************************************************************************************************************************************************/

/*
  This function implements the data transfer between function calls.
  The straighforward way of doing it would be to insert new parameters
  into each function declaration. However, passing data to functions
  through global variables is an easier alternative.
 */
GlobalVariable& Optimized::GetParamGlobal(Argument& param)
{
	static std::map<unsigned, GlobalVariable*> gvs;

	unsigned argNo = param.getArgNo();

	std::map<unsigned, GlobalVariable*>::iterator it = gvs.find(argNo);

	if (it != gvs.end()) 
	{
		GlobalVariable* gv = it->second;
		Type* oldType = gv->getType()->getElementType();
		int oldSize = targetData->getTypeSizeInBits(oldType);
		Type* newType = param.getType();
		int newSize = targetData->getTypeSizeInBits(newType);
		if (newSize > oldSize)
		{
			gv->mutateType(Type::getIntNPtrTy(*context, newSize)); //TODO: this is dangerous/not sure if the right choice
			gv->setInitializer(&GetNullValue(*Type::getIntNTy(*context, newSize)));
		}

		return *gv;
	}

	GlobalVariable* gv= new GlobalVariable(*module, ConvertType(param.getType()), false, GlobalVariable::CommonLinkage, &GetNullValue(*ConvertType(param.getType())), "");
	db("Created global variable: " << *gv);
	gvs.insert(std::pair<unsigned, GlobalVariable*>(argNo, gv));
	return *gv;

}

/*
  Convert data-structures to iX types.
 */
Type* Optimized::ConvertType(Type* type)
{
	return Type::getIntNTy(*context, targetData->getTypeSizeInBits(type));
}

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

/*
  AssertString is a function that checks if every character that makes up
  a string is tainted or clean. This function is defined externally.
 */
Function& Optimized::GetAssertStringFunction()
{
	static Function* func = 0;

	if (func) return *func;

	std::vector<Type*> argTypes;
	argTypes.push_back(Type::getInt8PtrTy(*context));
	func = Function::Create(FunctionType::get(Type::getVoidTy(*context), argTypes, false), GlobalValue::ExternalLinkage, "assertString", module);
	return *func;
}

/*
  This function inserts the abort code into the instrumented program. so far
  we are only checking if a printf can leak address. Thus, this function 
  goes over the string format argument of the printf function, and for each
  data that can be printed, it checks instruments this data. If the data
  might contain an address, then this function inserts an exit code there.
 */
void Optimized::HandlePrintf(CallSite* cs)
{
	db("Handling printf")
	CallSite::arg_iterator AI = cs->arg_begin();

	std::vector<bool> vaza, isString;
	Value *fmt = *AI;
	std::string formatString;
	bool hasFormat = false;

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(fmt)) {
		if (GlobalVariable *GV = dyn_cast<GlobalVariable>(CE->getOperand(0))) {
			if (ConstantArray *CA = dyn_cast<ConstantArray>(GV->getInitializer())) {
				if (CA->isString()) {
					formatString = CA->getAsString();
					hasFormat = true;
				}
			}
		}
	}

	int narg = 0;

	//TODO: This is not entirely correct. It ignores, for example, specifiers like %.3f etc

	if (!formatString.empty()) {


		int size = formatString.size();
		int i = 0;


		while (true)
		{
			while (i < size && formatString[i] != '%') i++;

			if (i == size) break;
			i++;

			if (i < size && isdigit(formatString[i]))
			{
				while (isdigit(formatString[i])) i++;
			}

			if (i < size && formatString[i] == '*')
			{
				vaza.push_back(false);
				isString.push_back(false);
				i++;
			}

			if (i < size && formatString[i] == '.')
			{
				i++;
				while (i < size && isdigit(formatString[i])) i++;
			}

			if (i < size)
			{
				switch (formatString[i])
				{
				case 'c':
				case 'e':
				case 'E':
				case 'f':
				case 'g':
				case 'G':
				case 'n':
				case 'L':
				case '%':
					vaza.push_back(false);
					isString.push_back(false);
					break;
				case 'd':
				case 'i':
				case 'o':
				case 'u':
				case 'x':
				case 'X':
				case 'p':
				case 'h':
				case 'l':
					vaza.push_back(true);
					isString.push_back(false);
					break;
				case 's':
					vaza.push_back(true);
					isString.push_back(true);
					break;
				}
			}

			i++;
		}
	}

	//			if (formatString[i] == '%') {
	//				if (formatString[i + 1] == 'c' ||
	//						formatString[i + 1] == 'e' ||
	//						formatString[i + 1] == 'E' ||
	//						formatString[i + 1] == 'f' ||
	//						formatString[i + 1] == 'g' ||
	//						formatString[i + 1] == 'G' ||
	//						formatString[i + 1] == 'n' ||
	//						formatString[i + 1] == 'L' ||
	//						formatString[i + 1] == '%') {
	//					vaza.push_back(false);
	//					isString.push_back(false);
	//					i++;
	//				} else if (formatString[i + 1] == 'd' ||
	//						formatString[i + 1] == 'i' ||
	//						formatString[i + 1] == 'o' ||
	//						formatString[i + 1] == 'u' ||
	//						formatString[i + 1] == 'x' ||
	//						formatString[i + 1] == 'X' ||
	//						formatString[i + 1] == 'p' ||
	//						formatString[i + 1] == 'h' ||
	//						formatString[i + 1] == 'l') {
	//					vaza.push_back(true);
	//					isString.push_back(false);
	//					i++;
	//				} else if (formatString[i + 1] == 's') { // Special case
	//
	//					vaza.push_back(true);
	//					isString.push_back(true);
	//					//					isString[vaza.size() - 1] = true;
	//					i++;
	//				}
	//			}
	//		}
	//	}

	for (int i = 0; i < vaza.size(); i++)
	{
		if (vaza[i])
		{
			if (isString[i])
			{

				Value* arg = cs->getArgument(i + 1);
				if (targetData->getTypeSizeInBits(arg->getType()) != targetData->getTypeSizeInBits(Type::getInt8PtrTy(*context)))
				{
					db("Strange printf: " << *cs->getInstruction());
				}
				assert(targetData->getTypeSizeInBits(arg->getType()) == targetData->getTypeSizeInBits(Type::getInt8PtrTy(*context)));
				BitCastInst* cast = new BitCastInst(arg, Type::getInt8PtrTy(*context), "", cs->getInstruction());
				assert(targetData->getTypeSizeInBits(Type::getInt8PtrTy(*context)) == targetData->getTypeSizeInBits(arg->getType()));
				Function& assertStringFunction = GetAssertStringFunction();
				std::vector<Value*> args;
				args.push_back(cast);
				CallInst::Create(&assertStringFunction, args, "", cs->getInstruction());
			}
			else
			{
				Value* arg = cs->getArgument(i + 1);

				Value& shadow = GetShadow(*arg);
				ICmpInst* cmp = new ICmpInst(cs->getInstruction(), CmpInst::ICMP_NE, &shadow, Constant::getNullValue(shadow.getType()), "");
				Function& assertZero = GetAssertZeroFunction();
				std::vector<Value*> args;
				args.push_back(cmp);
				CallInst* assertZeroCall = CallInst::Create(&assertZero, args, "", cs->getInstruction()); //TODO: memory leak?
			}
		}

	}


}

/*
  This function inserts the instrumented code. It does basically two things.
  First, it inserts instrumentation into the program to store in the shadow
  environment the abstract state of the variable defined by the instruction.
  Second, it goes over the def-use chain
  of variable v, and instruments each sink function that uses v.
  TODO: fix else code.
 */
void Optimized::HandleUses(Value& v)
{
	Value& shadow = GetShadow(v);

	for (Value::use_iterator it = v.use_begin(), end = v.use_end(); it != end; it++)
	{
		//TODO: this may be an invoke or something like that
		Value* call = dyn_cast<CallInst>(*it);

		if (! call)
		{
			call = dyn_cast<InvokeInst>(*it);  
		}	

		if (call) 
		{
			CallSite* cs = new CallSite(call);

			Function* f = cs->getCalledFunction();

			if (f == 0) continue;
			StringRef name = f->getName();

			if (name.equals("printf"))
			{
				HandlePrintf(cs);
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

/*
  This function instruments the code that will always require some
  instrumentation. There are functions that might be instrumented or
  not, depending on the result of the static analysis. However, there
  are also functions that must compulsorily be instrumented. Examples include
  stores and some libc functions, such as memcpy.
  TODO: move store code from handleUses to this place
 */
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

/*
  Inserts the instrumentation required by the memcpy function. This function
  always require instrumentation, because the C's semantics allows us to
  read an array outside its bounds. Thus, when facing this function we
  insert instrumentation that copies the abstract state from the source array's
  shadow into the target array's shadow.
 */
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
}

/*
  Starts the instrumentation framework, getting references to the required
  analysis, and allocating space for the data structures. This function is
  called immediately once runOnModule starts running.
 */
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
	if (dumb) MarkAsInstrumented(*init); //TODO: not sure if needed

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

/*
  This function converts an ordinary instruction into an instrumented
  instruction. This function is called over every instruction that
  must be instrumented. If we use the static analysis, some instructions will
  be deemd clear of tainted information. These instructions will not be given
  to the Instrument function.
 */
Value& Optimized::Instrument(Value& value)
{
	db("Instrumenting " << value)
																											//	MarkAsInstrumented(value);

	Value* shadow;

	Instruction* i;
	GlobalVariable* gv;
	Argument* param;
	Constant* c;

	i = dyn_cast<Instruction>(&value);

    // TODO: check if you can use an else here, to eliminate the gotos.
	if (i)
	{

		shadow = &Instrument(*i);
		goto end;
	}

	gv = dyn_cast<GlobalVariable>(&value);

	if (gv)
	{	
		shadow = &GetAllOnesValue(*ConvertType(gv->getType()));
		goto end;
	}

	param = dyn_cast<Argument>(&value);

	if (param)
	{
		db("Handling param");
		//TODO: Maybe there are ways to discover whether the param is constant

		GlobalVariable& paramGV = GetParamGlobal(*param);
		db("paramGV=" << paramGV);

		Function* f = param->getParent();
		assert(f != 0);
		BasicBlock& b = f->getEntryBlock();
		Instruction* firstInstruction = b.getFirstNonPHI();

		BitCastInst* cast = new BitCastInst(&paramGV, ConvertType(param->getType())->getPointerTo(), "", firstInstruction); //TODO: not sure if I need to specify the address space
		db("cast=" << *cast)
		assert(targetData->getTypeSizeInBits(ConvertType(param->getType())->getPointerTo()) == targetData->getTypeSizeInBits(paramGV.getType()));

		shadow = new LoadInst(cast, "", firstInstruction);
		db("shadow=" << *shadow)
		AddShadow(value, *shadow);
		HandleParamPassingTo(*f, param->getArgNo(), &paramGV);

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
	MarkAsInstrumented(*shadow);
	return *shadow;
}

/*
  Specific function to instrument the different types of instructions.
 */
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
	case Instruction::LandingPad:
	{
		LandingPadInst& lpad = cast<LandingPadInst>(instruction);
		lpad.inser
		break;
	}
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
	//		case Instruction::Resume:
	//		{
	//			//TODO: I'll do this later
	//			break;
	//		}
	case Instruction::ExtractValue: //TODO: This probably is wrong
	{
		ExtractValueInst& extract = cast<ExtractValueInst>(instruction);
		Value* agg = extract.getAggregateOperand();
		Value& shadow = GetShadow(*agg);
		BitCastInst* toAgg = new BitCastInst(&shadow, agg->getType(), "", &instruction);
		ExtractValueInst* shadowAgg = ExtractValueInst::Create(toAgg, extract.getIndices(), "", &instruction);
		newShadow = new BitCastInst(shadowAgg, ConvertType(shadowAgg->getType()), "", &instruction);
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
	//	case Instruction::Store:
	//	{
	//		return GetNullValue(*instruction.getType());
	//		//				assert(0 && 'Should never be there since this instruction does not produce a value');
	//		//			StoreInst& store = cast<StoreInst>(instruction);
	//		//			Value* pointer = store.getPointerOperand();
	//		//			Value& pointerShadow = GetShadowMemory(*pointer);
	//		//			Value* storedValue = store.getValueOperand();
	//		//			Value& storedValueShadow = GetShadow(*storedValue);
	//		//			StoreInst* shadowStore = new StoreInst(&storedValueShadow, &pointerShadow, &instruction); 
	//		//			MarkAsNotOriginal(*shadowStore);
	//		break;
	//	}
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
		//		Value& shadow = GetShadow(*bitcast.getOperand(0));
		//		newShadow = new BitCastInst(&shadow, bitcast.getDestTy(), "", &instruction);
		newShadow = &GetShadow(*bitcast.getOperand(0));

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
		newShadow = PHINode::Create(ConvertType(phi.getType()), 0, "", &instruction);  //TODO: I put NumReservedValues=0 because it's the safe choice and I don't know how to get the original one
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
	case Instruction::Invoke:
	{
		db("Handling invoke");
		InvokeInst& call = cast<InvokeInst>(instruction);
		Value* v = call.getCalledValue();

		if (call.getCalledFunction() != 0)
		{
			Function* func = call.getCalledFunction();

			if (! HasBody(*func))
			{	
				CallSite cs(&call);
				newShadow = HandleExternFunctionCall(cs);
				break;
			}
			else if (func->getReturnType() != Type::getVoidTy(*context)) {
				HandleReturns(*func);			
			}
		}

		Instruction* nextInstruction = GetNextInstruction(instruction);
		FunctionType* t = cast<FunctionType>(v->getType()->getContainedType(0));
		db("Function type=" << *t);
		Value& returnShadow = GetReturnGlobal(*t->getReturnType());
		db("Return shadow=" << returnShadow);
		newShadow = new LoadInst(&returnShadow, "", nextInstruction); 
		//		db("newShadow=" << *newShadow);

		break;
	}
	case Instruction::Call:
	{
		CallInst& call = cast<CallInst>(instruction);
		Value* v = call.getCalledValue();

		if (call.getCalledFunction() != 0)
		{
			Function* func = call.getCalledFunction();

			if (! HasBody(*func))
			{	
				CallSite cs(&call);
				newShadow = HandleExternFunctionCall(cs);
				break;
			}
			else if (func->getReturnType() != Type::getVoidTy(*context)) {
				HandleReturns(*func);			
			}
		}

		Instruction* nextInstruction = GetNextInstruction(instruction);
		db("Type is " << *v->getType());
		FunctionType* t = cast<FunctionType>(v->getType()->getContainedType(0));
		Value& returnShadow = GetReturnGlobal(*t->getReturnType());
		newShadow = new LoadInst(&returnShadow, "", nextInstruction); 

		break;
	}
	case Instruction::Trunc:    
	{
		TruncInst& trunc = cast<TruncInst>(instruction);
		Value& shadow = GetShadow(*trunc.getOperand(0));
		newShadow = new TruncInst(&shadow, ConvertType(trunc.getDestTy()), "", &instruction);
		break;
	}
	case Instruction::ZExt:     
	{
		ZExtInst& zext = cast<ZExtInst>(instruction);
		Value& shadow = GetShadow(*zext.getOperand(0));
		newShadow = new ZExtInst(&shadow, ConvertType(zext.getDestTy()), "", &instruction);
		break;
	}
	case Instruction::SExt:      
	{
		SExtInst& sext = cast<SExtInst>(instruction);
		Value& shadow = GetShadow(*sext.getOperand(0));
		newShadow = new SExtInst(&shadow, ConvertType(sext.getDestTy()), "", &instruction);
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
		Type* destType = ConvertType(conv.getDestTy());
		int destSize = targetData->getTypeSizeInBits(destType);
		Value& shadow = GetShadow(*conv.getOperand(0));
		Type* srcType = shadow.getType();
		int srcSize = targetData->getTypeSizeInBits(srcType);

		if (srcSize > destSize)
		{
			newShadow = new TruncInst(&shadow,  destType, "", &instruction); //TODO: this is not the best solution/is ugly/not sure if it works
		}
		else if (srcSize == destSize)
		{
			newShadow = &shadow;
		}
		else
		{
			newShadow = new SExtInst(&shadow, destType, "", &instruction); //TODO: this is not the best solution/is ugly/not sure if it works
		}


		break;
	}
	case Instruction::IntToPtr:  
	{
		IntToPtrInst& conv = cast<IntToPtrInst>(instruction);
		Value& shadow = GetShadow(*conv.getOperand(0));
		newShadow = new IntToPtrInst(&shadow, ConvertType(conv.getDestTy()), "", &instruction);
		break;
	}
	case Instruction::PtrToInt: 
	{
		PtrToIntInst& conv = cast<PtrToIntInst>(instruction);
		//		Value& shadow = GetShadow(*conv.getOperand(0));
		//		newShadow = new PtrToIntInst(&shadow, ConvertType(conv.getDestTy()), "", &instruction);
		newShadow = &GetShadow(*conv.getOperand(0));
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

	if (dumb && newShadow == 0)
	{
		newShadow = &GetNullValue(*instruction.getType());

		if (newShadow == 0)
		{
			newShadow = &GetNullValue(*Type::getInt32Ty(*context)); //TODO: ugly way to fix the problem of void not having a shadow. fix later
		}
	}

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

/*
  This function returns an external function that is inserted into the
  instrumented code. This function checks if the abstract state of a shadow
  value is tainted or clean. If that state is tainted, it aborts the program.
 */
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
			"myAbort",
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

/*
  This function returns an external function that maps program variables to
  shadow values.
 */
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
	Type* t = Type::getIntNTy(*context, targetData->getTypeSizeInBits(&type));
	return *Constant::getAllOnesValue(t);
}

Constant& Optimized::GetNullValue(Type& type)
{
	Type* t = Type::getIntNTy(*context, targetData->getTypeSizeInBits(&type));
	return *Constant::getNullValue(t);
}

bool Optimized::HasBody(Function& f)
{

	return f.getBasicBlockList().size() != 0;
}

/*
  This function inserts code to instrument return instructions. This
  instrumentation consists in writing into global variables the abstract
  state of the return value. If at least one return demands instrumentation,
  all the other returns will be instrumented.
 */
void Optimized::HandleReturns(Function& f)
{
	//TODO: remember to handle things like invoke vs call
	static std::set<Function*> alreadyHandled;

	std::set<Function*>::iterator it = alreadyHandled.find(&f);

	if (it != alreadyHandled.end()) return;

	alreadyHandled.insert(&f);


	GlobalVariable& returnGlobal = GetReturnGlobal(*f.getReturnType());



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
}

//void Optimized::HandleParam(Argument& arg)
//{
//	
//}

/*
  The abstract state of each function (its return value) is stored into a
  global variable. This function returns this global variable. There exists
  only one global value for each possible data type. So, many functions might
  shared the same global shadow storage for return values. However, this
  global storage has a very short life time, because it is only used to
  transport values from one function to another.
 */
GlobalVariable& Optimized::GetReturnGlobal(Type& type)
{
	static std::map<Type*, GlobalVariable*> returnShadows;

	std::map<Type*, GlobalVariable*>::iterator it = returnShadows.find(&type);

	if (it == returnShadows.end())
	{
		GlobalVariable* returnShadow = new GlobalVariable(*module, ConvertType(&type), false, GlobalVariable::CommonLinkage, &GetNullValue(*ConvertType(&type)), "");
		db("Created global variable: " << *returnShadow)
		returnShadows.insert(std::pair<Type*, GlobalVariable*>(&type, returnShadow));
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
	//	db("********************Instrumenting " << value << " resulted in " << shadow)
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

/*
  This function inserts the call to the translate external function into the
  instrumented program.
 */
Value& Optimized::CreateTranslateCall(Value& pointer, Instruction& before)
{

	Function& translateFunction = GetTranslateFunction();

	//first I must bitcast pointer to i8* since the translate function expects a i8* 
	BitCastInst* toi8p = new BitCastInst(&pointer, Type::getInt8PtrTy(*context), "", &before);
	assert(targetData->getTypeSizeInBits(Type::getInt8PtrTy(*context)) == targetData->getTypeSizeInBits(pointer.getType()));

	std::vector<Value*> args;
	args.push_back(toi8p);
	CallInst* call = CallInst::Create(&translateFunction, args, "", &before);

	Type* t = Type::getIntNPtrTy(*context, targetData->getTypeSizeInBits(cast<PointerType>(pointer.getType())->getElementType()));
	BitCastInst* fromi8p = new BitCastInst(call, t, "", &before);


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

// TODO: check if you can remove this function from the code.
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

/*
  This function handles the instrumentation of phi nodes. When we are
  instrumenting a phi-function, it is possible that some of its
  parameters have not been visited yet. If we called the handleUses
  on these parameters, we could enter an infinite loop.
 */
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

/*
  This function inserts instrumentation to copy the abstract values of actual
  parameters to formal parameters. The global variables are used to perform
  this data copy.
 */
void Optimized::HandleParamPassingTo(Function& f, int argno, Value* paramGV)
{
	for (Value::use_iterator it = f.use_begin()	, itEnd = f.use_end(); it != itEnd; it++)
	{
		//TODO: remember to handle invoke as well

		if (dyn_cast<CallInst>(*it) == 0 && dyn_cast<InvokeInst>(*it) == 0) continue; 

		//		if (! call)
		//		{

		//			assert(0 && "Forgot to test something");
		//		}

		CallSite cs(*it);
		Value* arg = cs.getArgument(argno);
		Value& shadow = GetShadow(*arg);



		BitCastInst* cast = new BitCastInst(paramGV, ConvertType(arg->getType())->getPointerTo(), "", cs.getInstruction());
		StoreInst* store = new StoreInst(&shadow, cast, cs.getInstruction()); //TODO: memory leak?

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

/*
  This function instruments calls to functions from external libraries.
  We are dealing with this function in a simplified way: if the function
  returns a pointer, we consider it to be tainted. Otherwise, we consider
  it clean. We ignore the parameters.
  TODO: this function breaks the correctness of our framework, in the
  sense that it is not conservative. We are still allowing the flow of
  information through integers, for instance. A more definitive solution
  would involve instrumenting in particular way every external function.
 */
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

// TODO: check if we can remove this code.
Instruction* Optimized::GetNextInstruction(Instruction& i)
{
	//TODO: This may not make sense since a instruction might have more than one sucessor or be the last instruction
	BasicBlock::iterator it(&i);
	it++;
	return it;
}

// TODO: check if we can remove this code.
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

/*
  This function decides which values need to be instrumented, and which
  do not.
 */
bool Optimized::MayBePointer(Value& v)
{
	if (dumb) return true;
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
