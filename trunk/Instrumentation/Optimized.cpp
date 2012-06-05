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

//TODO: Handle functions like memset
//TODO: Handle all instructions

/*********************************************************************************************************************************************************************************
 * PUBLIC METHODS IMPLEMENTATION
 *********************************************************************************************************************************************************************************/
Optimized::Optimized() : ModulePass(ID)
{

}

bool Optimized::runOnModule(Module& module)
{
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

					Instrument(*it);
				}

			}
		}
	}
	else
	{
		leakedValues = analysis->getLeakedValues();


		for (std::set<Value*>::iterator it = leakedValues.begin(); it != leakedValues.end(); it++) {

			//TODO: This may not be needed in the future
			if (dyn_cast<Function>(*it))
			{

				continue;
			}

			HandleUses(**it);
		}
	}

	InstrumentDelayedPHINodes();

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
	static std::map<Argument*, GlobalVariable*> gvs;
	std::map<Argument*, GlobalVariable*>::iterator it = gvs.find(&param);

	if (it != gvs.end()) 
	{
		return *it->second;
	}

	assert(! ConvertType(param.getType())->isVoidTy());
	GlobalVariable* gv= new GlobalVariable(*module, ConvertType(param.getType()), false, GlobalVariable::CommonLinkage, &GetNullValue(*ConvertType(param.getType())), "");
	gvs.insert(std::pair<Argument*, GlobalVariable*>(&param, gv));
	return *gv;

}


/*
  Convert data-structures to iX types.
 */

unsigned Optimized::GetSize(Type& type)
{
	if (type.getPrimitiveSizeInBits() != 0)
	{
		return type.getPrimitiveSizeInBits();
	}
	else
	{
		return targetData->getPointerSizeInBits();
	}	
}

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

	for (int i = 0; i < vaza.size(); i++)
	{
		if (vaza[i])
		{
			if (isString[i])
			{

				Value* arg = cs->getArgument(i + 1);

				BitCastInst* cast = new BitCastInst(arg, Type::getInt8PtrTy(*context), "", cs->getInstruction());
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
				MemCpyInst* mcy; 
				StoreInst* store;

				if (mcy = dyn_cast<MemCpyInst>(&*it)) 
				{
					HandleMemcpy(*mcy);
				} 
				//				else if (store = dyn_cast<StoreInst>(&*it)) 
				//				{
				//					Value& shadowPtr = CreateTranslateCall(*store->getPointerOperand(), *store);
				//					Value& shadow = GetShadow(*store->getValueOperand());
				//					StoreInst* x = new StoreInst(&shadow, &shadowPtr, store); //TODO: memory leak?
				//
				//				}
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
void Optimized::Setup(Module& module)
{
	analysis = &getAnalysis<AddrLeaks>();
	this->module = &module;
	this->context = &module.getContext();
	targetData = new TargetData(&module);

	Function& f = GetInitFunction();
	Function* main = module.getFunction("main");
	Instruction* first = main->getEntryBlock().getFirstNonPHI();
	CallInst* init = CallInst::Create(&f, "", first); 
	if (dumb) MarkAsInstrumented(*init); 
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
		//TODO: Maybe there are ways to discover whether the param is constant
		GlobalVariable& paramGV = GetParamGlobal(*param);

		Function* f = param->getParent();
		assert(f != 0);
		BasicBlock& b = f->getEntryBlock();
		Instruction* firstInstruction = b.getFirstNonPHI();

		BitCastInst* cast = new BitCastInst(&paramGV, ConvertType(param->getType())->getPointerTo(), "", firstInstruction); //TODO: not sure if I need to specify the address space


		shadow = new LoadInst(cast, "", firstInstruction);
		AddShadow(value, *shadow);
		HandleParamPassingTo(*f, param->getArgNo(), &paramGV);

		goto end;
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
	//TODO: uncomment the following code
	if (instruction.isBinaryOp())
	{
		BinaryOperator& bin = cast<BinaryOperator>(instruction);
		Value& op1 = *bin.getOperand(0);
		Value& op2 = *bin.getOperand(1);

		Value* shadow1 = &GetShadow(op1);
		Value* shadow2 = &GetShadow(op2);

		if (! shadow1->getType()->isIntegerTy()) 
		{
			assert(! shadow1->getType()->isIntegerTy());


			Type* newType;
			if (shadow1->getType()->isVectorTy())
			{
				VectorType* oldType = dyn_cast<VectorType>(shadow1->getType());
				newType = VectorType::get(Type::getIntNTy(*context, GetSize(*oldType->getScalarType())), oldType->getNumElements());
			}
			else
			{
				newType = Type::getIntNTy(*context, GetSize(*shadow1->getType()));
			}

			Type* oldType = shadow1->getType();
			Value* convertedShadow1 = new FPToUIInst(shadow1, newType, "", &instruction);
			Value* convertedShadow2 = new FPToUIInst(shadow2, newType, "", &instruction);
			Value* orOp = BinaryOperator::Create(Instruction::Or, convertedShadow1, convertedShadow2, "", &instruction);
			newShadow = new BitCastInst(orOp, oldType, "", &instruction);
		}
		else
		{
			newShadow = BinaryOperator::Create(Instruction::Or, shadow1, shadow2, "", &instruction);
		}


		return *newShadow;
	}

	switch (instruction.getOpcode())
	{
	// TODO: Handle LandingPad
	case Instruction::LandingPad:
	{
		//		LandingPadInst& lpad = cast<LandingPadInst>(instruction);
		//		lpad.inser
		break;
	}
	case Instruction::ExtractValue:
	{
		ExtractValueInst& extract = cast<ExtractValueInst>(instruction);
		Value* agg = extract.getAggregateOperand();
		Value& shadow = GetShadow(*agg);
		BitCastInst* toAgg = new BitCastInst(&shadow, agg->getType(), "", &instruction);
		ExtractValueInst* shadowAgg = ExtractValueInst::Create(toAgg, extract.getIndices(), "", &instruction);
		newShadow = new BitCastInst(shadowAgg, ConvertType(shadowAgg->getType()), "", &instruction);

		break;
	}
	case Instruction::Alloca:
	{

		//			//TODO: Add align info (I'm not sure of how relevant this is)
		AllocaInst& alloca = cast<AllocaInst>(instruction);
		newShadow = &GetAllOnesValue(*alloca.getType());
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
		break;
	}

	case Instruction::GetElementPtr:
	{
		GetElementPtrInst& gep = cast<GetElementPtrInst>(instruction);
		newShadow = &GetAllOnesValue(*gep.getType());
		break;
	}
	//@todo conversion operators
	case Instruction::BitCast:
	{
		BitCastInst& bitcast = cast<BitCastInst>(instruction);
		newShadow = &GetShadow(*bitcast.getOperand(0));	
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


		if (! t->getReturnType()->isVoidTy())
		{
			assert(! t->getReturnType()->isVoidTy());
			Value& returnShadow = GetReturnGlobal(*t->getReturnType());
			newShadow = new LoadInst(&returnShadow, "", nextInstruction);
		}
		else
		{
			newShadow = &GetInt(8, 0);
		}


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
		FunctionType* t = cast<FunctionType>(v->getType()->getContainedType(0));
		if (! t->getReturnType()->isVoidTy())
		{
			assert(! t->getReturnType()->isVoidTy());
			Value& returnShadow = GetReturnGlobal(*t->getReturnType());
			newShadow = new LoadInst(&returnShadow, "", nextInstruction);
		}
		else
		{
			newShadow = &GetInt(8, 0);
		}
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
	case Instruction::SIToFP:   
	{
		SIToFPInst& conv = cast<SIToFPInst>(instruction);
		Value& shadow = GetShadow(*conv.getOperand(0));
		newShadow = new SIToFPInst(&shadow, conv.getDestTy(), "", &instruction);
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
	default:
	{
		assert(false && "Should never reach here");
		break;
	}
	}

	end:

	if (dumb && newShadow == 0)
	{
		newShadow = &GetNullValue(*instruction.getType());

		if (newShadow == 0)
		{
			newShadow = &GetNullValue(*Type::getInt8Ty(*context)); //TODO: ugly way to fix the problem of void not having a shadow. fix later
		}
	}

	assert(newShadow != 0);

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
	Type* t = ConvertType(&type);

	return *Constant::getAllOnesValue(t);
}

Constant& Optimized::GetNullValue(Type& type)
{

	//	Type* t = Type::getIntNTy(*context, GetSize(type));
	Type* t = ConvertType(&type);

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
		assert(! type.isVoidTy());
		assert(! ConvertType(&type)->isVoidTy());
		GlobalVariable* returnShadow = new GlobalVariable(*module, ConvertType(&type), false, GlobalVariable::CommonLinkage, &GetNullValue(*ConvertType(&type)), "");

		returnShadows.insert(std::pair<Type*, GlobalVariable*>(&type, returnShadow));
		return *returnShadow;
	}

	return *it->second;
}

void Optimized::AddShadow(Value& value, Value& shadow)
{
	valueToShadow.insert(std::pair<Value*, Value*>(&value, &shadow));
}

Value& Optimized::GetShadow(Value& value)
{
	std::map<Value*, Value*>::iterator it = valueToShadow.find(&value);

	if (it != valueToShadow.end()) return *it->second;

	if (! MayBePointer(value))
	{
		return GetNullValue(*value.getType());
	}

	Value& shadow = Instrument(value);
	return shadow;
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

	std::vector<Value*> args;
	args.push_back(toi8p);
	CallInst* call = CallInst::Create(&translateFunction, args, "", &before);


	//	Type* t = Type::getIntNPtrTy(*context, GetSize(*cast<PointerType>(pointer.getType())->getElementType()));
	BitCastInst* fromi8p = new BitCastInst(call, pointer.getType(), "", &before);



	return *fromi8p;
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
}

/*
  This function inserts instrumentation to copy the abstract values of actual
  parameters to formal parameters. The global variables are used to perform
  this data copy.
 */
void Optimized::HandleParamPassingTo(Function& f, int argno, Value* paramGV)
{
	for (Value::use_iterator it = f.use_begin()	, itEnd = f.use_end(); it != itEnd; it++)
	{

		if (dyn_cast<CallInst>(*it) == 0 && dyn_cast<InvokeInst>(*it) == 0) continue; 

		CallSite cs(*it);
		Value* arg = cs.getArgument(argno);
		Value& shadow = GetShadow(*arg);



		BitCastInst* cast = new BitCastInst(paramGV, ConvertType(arg->getType())->getPointerTo(), "", cs.getInstruction());
		StoreInst* store = new StoreInst(&shadow, cast, cs.getInstruction()); //TODO: memory leak?

	}
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


	Type* retType = func.getReturnType();
	int size = GetSize(*retType);
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
