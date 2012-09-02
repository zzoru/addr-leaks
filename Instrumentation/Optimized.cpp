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
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

#include "../AddrLeaks/AddrLeaks.cpp"

#include <cmath>
#include <set>
#include <map>
#include <sstream>
#include <vector>

#define null 0

using namespace llvm;

#define DEBUG
#ifndef DEBUG
#define db(x) ;;;
#else
#define db(x) errs() << x << "\n";
#endif

cl::opt<bool> IsDumb("d", cl::desc("Specify whether to use the static analysis"), cl::desc("is dumb"));
cl::opt<bool> UsePointerAnalysis("panalysis", cl::desc("Specify whether to use the pointer analysis"), cl::desc("use pointer analysis"));
cl::opt<bool> Continue("c", cl::desc("Specify whether to continue the execution after detecting a leak"), cl::desc("continue execution"));

class Optimized : public ModulePass
{	
	Module* module;
	LLVMContext* context;
	TargetData* targetData;
	std::map<PHINode*, PHINode*> delayedPHINodes;
	AddrLeaks* analysis;

	std::set<std::pair<Function*, Argument*> > callsToBeHandled;
	std::set<Function*> returnsToBeHandled;

	std::set<Value*> storeValuesThatMustBeInstrumented;

	bool taggedAStore;

	bool dumb;
	bool continueExecution;
	bool usePointerAnalysis;

public:
	static char ID;

	Optimized() : ModulePass(ID)
	{
		dumb = IsDumb;
	}

	std::set<Value*> dirtyValues; //These are the values that are possible dirty and that aren't instructions. If not using a static analysis, this is empty since every value is possible dirty and it wouldn't make
	//sense to store ALL values in there

	virtual bool runOnModule(Module& module)
	{
		dumb = IsDumb;
		taggedAStore = dumb;
		continueExecution = Continue;
		usePointerAnalysis = UsePointerAnalysis;

		Setup(module);
		HandleSpecialFunctions(); //TODO: Must be first so the code that I added will not be instrumented. This solution is inelegant considering how the rest of the program handles these things

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
			analysis = &getAnalysis<AddrLeaks>();
			std::set<Value*> leakedValues = analysis->getLeakedValues();

			for (std::set<Value*>::iterator it = leakedValues.begin(); it != leakedValues.end(); it++)
			{
				Instruction* i = dyn_cast<Instruction>(*it);

				if (i)
				{
					AddMetadata(*i, "dirty");
				}
				else
				{
					dirtyValues.insert(*it);
				}
			}

			for (std::set<Value*>::iterator it = leakedValues.begin(); it != leakedValues.end(); it++)
			{
				//TODO: This may not be needed in the future
				if (dyn_cast<Function>(*it)) continue;

				Instruction* i = dyn_cast<Instruction>(*it);

				if (i && ! AlreadyInstrumented(*i))
				{
					Instrument(*i);
				}
			}
		}

		InstrumentStores();


		for (std::set<Function*>::iterator it = returnsToBeHandled.begin(), itEnd = returnsToBeHandled.end(); it != itEnd; it++)
		{
			HandleReturns(**it);
		}


		for (std::set<std::pair<Function*, Argument*> >::iterator it = callsToBeHandled.begin(), itEnd = callsToBeHandled.end(); it != itEnd; it++)
		{
			HandleParamPassingTo(*it->first, *it->second);
		}


		HandleSinkCalls();

		while (! delayedPHINodes.empty())
		{
			InstrumentDelayedPHINodes();
		}

		return true;
	}

private:
	void InstrumentStore(StoreInst& store)
	{
		Value& shadowPtr = CreateTranslateCall(*store.getPointerOperand(), store);
		Value& shadow = GetShadow(*store.getValueOperand());
		StoreInst* shadowStore = new StoreInst(&shadow, &shadowPtr, &store); //TODO: Is there any way to do something like Store::Create?
		MarkAsInstrumented(*shadowStore);
		MarkAsInstrumented(store);
	}

	void InstrumentAtomicRMW(AtomicRMWInst& inst)
	{
		//TODO: implement this.
	}

	void InstrumentCmpXchg(AtomicCmpXchgInst& cmpxchg)
	{
		//TODO: implement this.
	}

	//TODO: Maybe there's a way to join the 3 if's.
	void InstrumentStores()
	{
		while (taggedAStore)
		{
			taggedAStore = false; 

			for (Module::iterator funcIt = module->begin(), funcItEnd = module->end(); funcIt != funcItEnd; funcIt++)
			{
				for (inst_iterator it = inst_begin(*funcIt), itEnd = inst_end(*funcIt); it != itEnd; it++)
				{
					StoreInst* store = dyn_cast<StoreInst>(&*it);

					if (store && ! AlreadyInstrumented(*store))
					{
						Value* v = store->getValueOperand();
						Instruction* i = dyn_cast<Instruction>(v);

						if (! usePointerAnalysis || (i && HasMetadata(*i, "must-instrument-store")) || (! i && storeValuesThatMustBeInstrumented.find(v) != storeValuesThatMustBeInstrumented.end()))
						{
							InstrumentStore(*store);
						}
					}

					AtomicRMWInst* armw = dyn_cast<AtomicRMWInst>(&*it);

					if (armw && ! AlreadyInstrumented(*armw)) 
					{
						Value* v = armw->getValOperand();
						Instruction* i = dyn_cast<Instruction>(v);

						if (! usePointerAnalysis || (i && HasMetadata(*i, "must-instrument-store")) || (! i && storeValuesThatMustBeInstrumented.find(v) != storeValuesThatMustBeInstrumented.end()))
						{
							InstrumentAtomicRMW(*armw);
						}
					}

					AtomicCmpXchgInst* cmpxchg = dyn_cast<AtomicCmpXchgInst>(&*it);

					if (cmpxchg && ! AlreadyInstrumented(*cmpxchg)) 
					{
						Value* v = cmpxchg->getNewValOperand();
						Instruction* i = dyn_cast<Instruction>(v);

						if (! usePointerAnalysis || (i && HasMetadata(*i, "must-instrument-store")) || (! i && storeValuesThatMustBeInstrumented.find(v) != storeValuesThatMustBeInstrumented.end()))
						{
							InstrumentCmpXchg(*cmpxchg);
						}
					}
				}
			}
		}
	}

	void HandleSinkCalls()
	{
		if (! dumb)
		{
			std::vector<std::pair<Instruction*, std::vector<Value*> > > leakedInSink = analysis->getPrintfLeaks();

			for (std::vector<std::pair<Instruction*, std::vector<Value*> > >::iterator it = leakedInSink.begin(), itEnd = leakedInSink.end(); it != itEnd; it++)
			{
				Instruction* i = it->first;
				std::vector<Value*> vs = it->second;

				for (std::vector<Value*>::iterator vIt = vs.begin(), vEnd = vs.end(); vIt != vEnd; vIt++)
				{
					CallSite cs(i);
					HandlePrintf(&cs, *vIt);
				}
			}
		}
		else
		{
			Function* printfFunction = module->getFunction("printf");

			if (printfFunction == 0) return;

			for (Function::use_iterator it = printfFunction->use_begin(), itEnd = printfFunction->use_end(); it != itEnd; it++)
			{
				if (dyn_cast<CallInst>(*it) || dyn_cast<InvokeInst>(*it))
				{
					CallSite cs(*it);
					HandlePrintf(&cs, 0);
				}
			}
		}
	}

	void AddAssertCode(Value& shadow, Instruction& sinkCall)
	{
		Type* iN = Type::getIntNTy(*context, GetSize(*shadow.getType()));
		CastInst* cast;

		if (shadow.getType()->isPointerTy())
		{
			cast = CastInst::Create(Instruction::PtrToInt, &shadow, iN, "", &sinkCall);
		}
		else
		{
			cast = CastInst::Create(Instruction::BitCast, &shadow, iN, "", &sinkCall);
		}


		MarkAsInstrumented(*cast);

		ICmpInst* cmp = new ICmpInst(&sinkCall, CmpInst::ICMP_NE, cast, &GetNullValue(*iN), "");
		Function& assertZero = GetAssertZeroFunction();
		std::vector<Value*> args;
		args.push_back(cmp);
		CallInst* call = CallInst::Create(&assertZero, args, "", &sinkCall);
		MarkAsInstrumented(*call);
	}

	void AddStringAssertCode(Value& string, Instruction& sinkCall)
	{
		Function& assertZeroStringFunction = GetAssertZeroStringFunction();
		CastInst* v = CastInst::Create(CastInst::BitCast, &string, Type::getInt8PtrTy(*context), "", &sinkCall);
		std::vector<Value*> args;
		args.push_back(v);
		Value* str = sinkCall.getOperand(0);	
		CallInst* call = CallInst::Create(&assertZeroStringFunction, args, "", &sinkCall);
		MarkAsInstrumented(*call);
	}

	unsigned GetAllocSize(Type& type)
	{
		return targetData->getTypeAllocSizeInBits(&type);
	}

	GlobalVariable& GetParamGlobal(Type& type, unsigned argNo)
	{
		static std::map<unsigned, GlobalVariable*> gvs;

		std::map<unsigned, GlobalVariable*>::iterator it = gvs.find(argNo);

		unsigned size = GetAllocSize(type);
		Type* iNType = Type::getIntNTy(*context, size);

		if (it != gvs.end())
		{
			unsigned oldSize = GetAllocSize(*it->second->getType());

			if (size > oldSize)
			{
				it->second->mutateType(iNType->getPointerTo());
				it->second->setInitializer(&GetNullValue(*iNType));
			}

			return *it->second;
		}


		GlobalVariable* gv= new GlobalVariable(*module, iNType, false, GlobalVariable::CommonLinkage, &GetNullValue(*iNType), "");
		gvs.insert(std::pair<unsigned, GlobalVariable*>(argNo, gv));
		return *gv;
	}

	unsigned GetSize(Type& type)
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

	void HandlePrintf(CallSite* cs, Value* v)
	{
		CallSite::arg_iterator AI = cs->arg_begin();

		std::vector<bool> vaza, isString;
		Value *fmt = *AI;
		std::string formatString;
		bool hasFormat = false;

		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(fmt)) {
			if (GlobalVariable *GV = dyn_cast<GlobalVariable>(CE->getOperand(0))) {
				ConstantDataArray *CA;
				if ((CA = dyn_cast<ConstantDataArray>(GV->getInitializer()))) {
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
			if (vaza[i] && (cs->getArgument(i + 1) == v || v == 0))
			{
				if (isString[i])
				{
					Value* arg = cs->getArgument(i + 1);
					AddStringAssertCode(*arg, *cs->getInstruction());

				}
				else
				{
					Value* arg = cs->getArgument(i + 1);
					Value& shadow = GetShadow(*arg);
					AddAssertCode(shadow, *cs->getInstruction());
				}
			}
		}
	}

	void HandleSpecialFunctions()
	{
		for (Module::iterator fIt = module->begin(), fItEnd = module->end(); fIt != fItEnd; ++fIt) {
			if (! fIt->isDeclaration()) {
				for (inst_iterator it = inst_begin(fIt), itEnd = inst_end(fIt); it != itEnd; ++it)
				{
					MemCpyInst* mcy;
					StoreInst* store;

					if ((mcy = dyn_cast<MemCpyInst>(&*it)))
					{
						HandleMemcpy(*mcy);
					}
				}
			}
		}
	}

	void HandleMemcpy(MemCpyInst& i)
	{
		Function* f = i.getCalledFunction();
		Value* src = i.getRawSource();
		Value& newSrc = CreateTranslateCall(*src, i);
		Value* dest = i.getRawDest();
		Value& newDest = CreateTranslateCall(*dest, i);
		std::vector<Value*> args;
		args.push_back(&newDest);
		args.push_back(&newSrc);
		args.push_back(i.getLength()); //TODO: these args might be wrong
		args.push_back(i.getAlignmentCst());
		args.push_back(i.getVolatileCst());
		CallInst* call = CallInst::Create(f, args, "", &i);
		MarkAsInstrumented(*call);
	}

	std::vector<Value*> GetPointsToSet(Value& v)
																																																					{
		PointerAnalysis* pointerAnalysis = analysis->getPointerAnalysis();

		int i = analysis->Value2Int(&v);
		std::set<int>  intSet = pointerAnalysis->pointsTo(i);

		std::vector<Value*> valuesSet;

		for (std::set<int> ::iterator it = intSet.begin(), itEnd = intSet.end(); it != itEnd; ++it)
		{
			valuesSet.push_back(analysis->Int2Value(*it));
		}

		return valuesSet;
																																																					}


	void HandleStoresIn(Value& pointer)
	{
		std::vector<Value*> pointsTo = GetPointsToSet(pointer);

		for (std::vector<Value*>::iterator it = pointsTo.begin(), itEnd = pointsTo.end(); it != itEnd; ++it)
		{
			if (!(*it)) {
				if (storeValuesThatMustBeInstrumented.find(*it) == storeValuesThatMustBeInstrumented.end())
				{
					storeValuesThatMustBeInstrumented.insert(*it);
					taggedAStore = true;
				}
			}
			else
			{
				Instruction* i = dyn_cast<Instruction>(*it);

				if (i && ! HasMetadata(*i, "must-instrument-store"))
				{
					AddMetadata(*i, "must-instrument-store");
					taggedAStore = true;

				}
			}
		}
	}

	void AddSetupToMainFunction()
	{
		Function& f = GetInitFunction();
		Function* main = module->getFunction("main");
		Instruction* first = main->getEntryBlock().getFirstNonPHI();
		CallInst* init = CallInst::Create(&f, "", first);
		MarkAsInstrumented(*init);
	}

	void AddSetupToGlobalCtor()
	{
		GlobalVariable* gv = module->getGlobalVariable("llvm.global_ctors");

		if (! gv) return; //TODO: not sure if this is needed

		Value* v = gv->getInitializer();
		ConstantArray* carray = dyn_cast<ConstantArray>(v);

		if (! carray) return;

		for (ConstantArray::op_iterator it = carray->op_begin(), itEnd = carray->op_end(); it != itEnd; ++it)
		{
			ConstantStruct& cs = cast<ConstantStruct>(**it);	
			Function& func = cast<Function>(*cs.getAggregateElement(1));
			AddSetupCodeToFunction(func);
		}
	}

	void AddSetupCodeToFunction(Function& f)
	{
		Function& initFunc = GetInitFunction();
		Instruction* first = f.getEntryBlock().getFirstNonPHI();
		CallInst* init = CallInst::Create(&initFunc, "", first);
		MarkAsInstrumented(*init);
	}

	void Setup(Module& module)
	{	
		this->module = &module;
		this->context = &module.getContext();
		targetData = new TargetData(&module);

		Function& main = *module.getFunction("main");
		AddSetupCodeToFunction(main);

		AddSetupToGlobalCtor();
	}
	void Instrument(Instruction& instruction)
	{
		static char ident[100] = "";
		static int i = 0;
		ident[i] = 0;

		if (isa<StoreInst>(&instruction)) return;
		MarkAsInstrumented(instruction);

		Value* newShadow = 0;

		if (instruction.isBinaryOp())
		{
			BinaryOperator& bin = cast<BinaryOperator>(instruction);
			Value& op1 = *bin.getOperand(0);
			Value& op2 = *bin.getOperand(1);

			Value& shadow1 = GetShadow(op1);
			Value& shadow2 = GetShadow(op2);

			if (! shadow1.getType()->isIntegerTy())
			{
				assert(! shadow2.getType()->isIntegerTy());

				Type* newType;
				if (shadow1.getType()->isVectorTy())
				{
					VectorType& oldType = cast<VectorType>(*shadow1.getType());
					newType = VectorType::get(Type::getIntNTy(*context, GetSize(*oldType.getScalarType())), oldType.getNumElements());
				}
				else
				{
					newType = Type::getIntNTy(*context, GetSize(*shadow1.getType()));
				}

				Type* oldType = shadow1.getType();



				CastInst* convertedShadow1 = CastInst::Create(Instruction::BitCast, &shadow1, newType, "", &instruction);
				CastInst* convertedShadow2 = CastInst::Create(Instruction::BitCast, &shadow2, newType, "", &instruction);



				Instruction* orOp = BinaryOperator::Create(Instruction::Or, convertedShadow1, convertedShadow2, "", &instruction);
				newShadow = CastInst::Create(Instruction::BitCast, orOp, oldType, "", &instruction);

				MarkAsInstrumented(*convertedShadow1);
				MarkAsInstrumented(*convertedShadow2);
				MarkAsInstrumented(*orOp);
			}
			else
			{
				newShadow = BinaryOperator::Create(Instruction::Or, &shadow1, &shadow2, "", &instruction);
			}

			goto end;
		}

		switch (instruction.getOpcode())
		{
		case Instruction::ShuffleVector:
		{
			//TODO: this is just a placeholder so that the pass doesn't abort on programs that use this instruction
			newShadow = &GetAllOnesValue(*instruction.getType());
			break;
		}
		case Instruction::AtomicRMW:
		{
			//TODO: this is just a placeholder so that the pass doesn't abort on programs that use this instruction. 
			newShadow = &GetAllOnesValue(*instruction.getType());
			break;
		}
		case Instruction::AtomicCmpXchg:
		{
			//TODO: this is just a placeholder so that the pass doesn't abort on programs that use this instruction.
			newShadow = &GetAllOnesValue(*instruction.getType());
			break;
		}
		case Instruction::LandingPad:
		{
			LandingPadInst& landingPad = cast<LandingPadInst>(instruction);
			newShadow = &GetNullValue(*landingPad.getType());
			break;
		}
		case Instruction::ExtractValue:
		{
			ExtractValueInst& extract = cast<ExtractValueInst>(instruction);
			Value* agg = extract.getAggregateOperand();
			Value& shadow = GetShadow(*agg);
			newShadow = ExtractValueInst::Create(&shadow, extract.getIndices(), "", &instruction);
			break;
		}
		case Instruction::InsertValue:
		{
			InsertValueInst& insert = cast<InsertValueInst>(instruction);
			Value* agg = insert.getAggregateOperand();
			Value& aggShadow = GetShadow(*agg);
			Value* value = insert.getInsertedValueOperand();
			Value& shadow = GetShadow(*value);
			newShadow = InsertValueInst::Create(&aggShadow, value, insert.getIndices(), "", &instruction);
			break;
		}
		case Instruction::ExtractElement:
		{
			ExtractElementInst& ee = cast<ExtractElementInst>(instruction);
			Value& shadow = GetShadow(*ee.getOperand(0));
			newShadow = ExtractElementInst::Create(&shadow, ee.getIndexOperand(), "", &instruction);
			break;
		}
		case Instruction::InsertElement:
		{
			//TODO: Not sure if implemented correctly
			InsertElementInst& ie = cast<InsertElementInst>(instruction);
			Value& shadow = GetShadow(*ie.getOperand(0));
			Value& scalarShadow = GetShadow(*ie.getOperand(1));
			newShadow = InsertElementInst::Create(&shadow, &scalarShadow, ie.getOperand(2), "", &instruction);
			break;
		}
		case Instruction::Alloca:
		{
			//TODO: Add align info (I'm not sure of how relevant this is)
			AllocaInst& alloca = cast<AllocaInst>(instruction);
			newShadow = &GetAllOnesValue(*alloca.getType());
			break;
		}
		case Instruction::Load:
		{
			LoadInst& load = cast<LoadInst>(instruction);
			Value* pointer = load.getPointerOperand();
			Value& pointerShadowMemory = CreateTranslateCall(*pointer, load);
			newShadow = new LoadInst(&pointerShadowMemory, "", &instruction);
			if (! dumb) HandleStoresIn(*pointer);
			break;
		}
		case Instruction::GetElementPtr:
		{
			GetElementPtrInst& gep = cast<GetElementPtrInst>(instruction);
			newShadow = &GetAllOnesValue(*gep.getType());
			break;
		}

		/*
		    case Instruction::PtrToInt: // TODO: Check if this should be used for the other cast instructions below
		    {
		        //CastInst& ci = cast<CastInst>(instruction);
		        //Value *pointer = ci.getOperand(0);

		        //Value& shadowPtr = CreateTranslateCall(*pointer, ci);
		        //Type* iNType = Type::getIntNTy(*context, targetData->getPointerSizeInBits());
		        //Type* type = pointer->getType();
		        //Type* type2 = cast<SequentialType>(type)->getElementType();
				//Value& shadow = GetAllOnesValue(*type2);

		        //StoreInst* shadowStore = new StoreInst(&shadow, &shadowPtr, &ci);
		        //newShadow = shadowStore;

		        //Type* iNType = Type::getIntNTy(*context, targetData->getPointerSizeInBits());
		        //CastInst& ci = cast<CastInst>(instruction);
		        //Value *pointer = ci.getOperand(0);

		        Type* iNType = Type::getInt32Ty(*context);
		        newShadow = &GetAllOnesValue(*iNType);
		        break;
		    }
		 */
		case Instruction::PtrToInt:
		case Instruction::FPExt:
		case Instruction::FPTrunc:
		case Instruction::BitCast:
		case Instruction::Trunc:
		case Instruction::ZExt:
		case Instruction::SExt:
		case Instruction::IntToPtr:
		case Instruction::SIToFP:
		case Instruction::FPToSI:
		case Instruction::UIToFP:
		case Instruction::FPToUI:
		{
			CastInst& ci = cast<CastInst>(instruction);
			Value *v = ci.getOperand(0);
			newShadow = CastInst::Create(ci.getOpcode(), &GetShadow(*v), ci.getDestTy(), "", &instruction);
			break;
			/*
				CastInst& ci = cast<CastInst>(instruction);
				newShadow = CastInst::Create(ci.getOpcode(), ci.getOperand(0), ci.getDestTy(), "", &instruction);
				break;
			 */
		}
		case Instruction::ICmp:
		case Instruction::FCmp:
		{
			CmpInst& cmp = cast<CmpInst>(instruction);
			newShadow = &GetNullValue(*instruction.getType()); //TODO: Not 100% sure that a null value of a vector represent a vector of null values
			break;
		}
		case Instruction::PHI:
		{
			PHINode& phi = cast<PHINode>(instruction);
			newShadow = PHINode::Create(phi.getType(), 0, "", &instruction);  //TODO: I put NumReservedValues=0 because it's the safe choice and I don't know how to get the original one
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
		case Instruction::Call:
		{
			CallSite cs(&instruction);

			if (cs.getType()->isVoidTy()) return;

			if (cs.getCalledFunction() == 0 || (cs.getCalledFunction() != 0 && HasBody(*cs.getCalledFunction())))
			{
				if (cs.getCalledFunction() != 0 && HasBody(*cs.getCalledFunction()))
				{
					returnsToBeHandled.insert(cs.getCalledFunction());
				}
				else
				{
					unsigned argNo = 0;

					for (CallSite::arg_iterator it = cs.arg_begin(), itEnd = cs.arg_end(); it != itEnd; it++)
					{
						Value& shadow = GetShadow(*it->get());
						GlobalVariable& gv = GetParamGlobal(*it->get()->getType(), argNo);
						CastInst *cast = CastInst::Create(Instruction::BitCast, &gv, shadow.getType()->getPointerTo(), "", &instruction);

						MarkAsInstrumented(*cast);
						StoreInst* store = new StoreInst(&shadow, cast, &instruction); //TODO: isn't there something like StoreInst::Create?

						MarkAsInstrumented(*store);
						argNo++;
					}
				}


				CallInst* call = dyn_cast<CallInst>(&instruction);

				if (call)
				{
					Instruction* next = GetNextInstruction(instruction);	
					GlobalVariable& gv = GetReturnGlobal(*cs.getType());
					CastInst* cast = CastInst::Create(Instruction::BitCast, &gv, instruction.getType()->getPointerTo(), "", next);	
					MarkAsInstrumented(*cast);
					newShadow = new LoadInst(cast, "", next);
				}
				else
				{
					InvokeInst& invoke = cast<InvokeInst>(instruction);

					//TODO: I'm assuming that getNextNode returns 0 when there isn't a next node (ie. the block is the last one)

					BasicBlock* tmpBlock = BasicBlock::Create(*context, "", invoke.getParent()->getParent(), invoke.getParent()->getNextNode());
					BasicBlock* normalDest = invoke.getNormalDest();
					BasicBlock* invokeBlock = invoke.getParent();
					
					//updating the phis
					for (BasicBlock::iterator it = normalDest->begin(), itEnd = normalDest->end(); it != itEnd; ++it)
					{
						PHINode* phi = dyn_cast<PHINode>(it);

						if (! phi) break;
						
						int i = 0;
						for (PHINode::block_iterator blockIt = phi->block_begin(), blockItEnd = phi->block_end(); blockIt != blockItEnd; ++blockIt)
						{
							if (*blockIt == invokeBlock)
							{
								phi->setIncomingBlock(i, tmpBlock);
							}
							
							++i;
						}
					}

					invoke.setNormalDest(tmpBlock);

					GlobalVariable& gv = GetReturnGlobal(*cs.getType());
					CastInst* cast = CastInst::Create(Instruction::BitCast, &gv, instruction.getType()->getPointerTo(), "", tmpBlock);	
					MarkAsInstrumented(*cast);
					Instruction* load = new LoadInst(cast, "", tmpBlock);
					BranchInst* branch = BranchInst::Create(normalDest, tmpBlock);
					MarkAsInstrumented(*branch);
					newShadow = load;
				}
			}
			else
			{
				newShadow = HandleExternFunctionCall(cs);
			}
			break;
		}
		default:
		{
			assert(dumb && "Should never reach here");
			break;
		}
		}

		end:
		assert(dumb || newShadow != 0);



		if (newShadow != 0)
		{
			AddShadow(instruction, *newShadow);

			Instruction* i = dyn_cast<Instruction>(newShadow);
			if (i)
			{
				MarkAsInstrumented(*i);
			}

		}
	}

	Function& GetAssertZeroStringFunction()
	{
		static Function* assertZeroString = 0;
		if (assertZeroString != 0) return *assertZeroString;

		std::vector<Type*> assertParams;
		assertParams.push_back(Type::getInt8PtrTy(*context));
		assertZeroString = Function::Create(FunctionType::get(Type::getVoidTy(*context),
				assertParams,
				false),
				GlobalValue::ExternalLinkage,
				"assertZeroString",
				module);
		return *assertZeroString;
	}

	Function& GetAssertZeroFunction()
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

		Function *quitProgram;

		if (continueExecution)
		{
			quitProgram = Function::Create(FunctionType::get(Type::getVoidTy(*context), false),
					GlobalValue::ExternalLinkage,
					"myAbort2",
					module);
		}
		else
		{
			quitProgram = Function::Create(FunctionType::get(Type::getVoidTy(*context), false),
					GlobalValue::ExternalLinkage,
					"myAbort",
					module);
		}

		BasicBlock* entry = BasicBlock::Create(*context, "entry", assertZero);
		Function::ArgumentListType& argList = assertZero->getArgumentList();
		ICmpInst* cmp = new ICmpInst(*entry, ICmpInst::ICMP_EQ, &argList.front(), ConstantInt::get(Type::getInt1Ty(*context), 0));

		BasicBlock* ifTrue = BasicBlock::Create(*context, "", assertZero);
		ReturnInst* ret = ReturnInst::Create(*context, ifTrue);

		BasicBlock* ifFalse = BasicBlock::Create(*context, "", assertZero);

		CallInst* call = CallInst::Create(quitProgram, "", ifFalse);

		//UnreachableInst* unr = new UnreachableInst(*context, ifFalse);
		ReturnInst* ret2 = ReturnInst::Create(*context, ifFalse);

		BranchInst* branch = BranchInst::Create(ifTrue, ifFalse, cmp, entry);

		MarkAsInstrumented(*cmp);
		MarkAsInstrumented(*ret);
		MarkAsInstrumented(*call);
		//MarkAsInstrumented(*unr);
		MarkAsInstrumented(*ret2);

		return GetAssertZeroFunction();
	}

	Function& GetTranslateFunction()
	{
		static Function* func = 0;

		if (func) return *func;

		std::vector<Type*> args;
		args.push_back(Type::getInt8PtrTy(*context));
		func = Function::Create(FunctionType::get(Type::getInt8PtrTy(*context), args, false), GlobalValue::ExternalLinkage, "translate", module);
		return *func;
	}
	Value& CreateTranslateCall(Value& pointer, Instruction& before)
	{
		Function& translateFunction = GetTranslateFunction();

		//first I must bitcast pointer to i8* since the translate function expects a i8*
		CastInst* toi8p = CastInst::Create(Instruction::BitCast, &pointer, Type::getInt8PtrTy(*context), "", &before);


		std::vector<Value*> args;
		args.push_back(toi8p);
		CallInst* call = CallInst::Create(&translateFunction, args, "", &before);
		CastInst* fromi8p = CastInst::Create(Instruction::BitCast, call, pointer.getType(), "", &before);

		MarkAsInstrumented(*toi8p);
		MarkAsInstrumented(*call);
		MarkAsInstrumented(*fromi8p);

		return *fromi8p;
	}
	Function& GetInitFunction()
	{
		static Function* func = 0;

		if (func) return *func;

		func = Function::Create(FunctionType::get(Type::getVoidTy(*context), false), GlobalValue::ExternalLinkage, "createShadowMemory", module);
		return *func;
	}
	Constant& GetAllOnesValue(Type& type)
	{
		if (type.isPointerTy())
		{
			Type* iNType = Type::getIntNTy(*context, targetData->getPointerSizeInBits());
			Constant* iNValue = &GetAllOnesValue(*iNType);
			return *ConstantExpr::getIntToPtr(iNValue, &type);
		}
		else
		{
			return *Constant::getAllOnesValue(&type);
		}
	}
	Constant& GetNullValue(Type& type)
	{
		return *Constant::getNullValue(&type);
	}
	bool HasBody(Function& f)
	{
		return f.getBasicBlockList().size() != 0;
	}
	void HandleReturns(Function& f)
	{
		static std::set<Function*> alreadyHandled;

		std::set<Function*>::iterator it = alreadyHandled.find(&f);

		if (it != alreadyHandled.end()) return;

		alreadyHandled.insert(&f);


		GlobalVariable& returnGlobal = GetReturnGlobal(*f.getReturnType());

		//TODO: Maybe there is a way to go to the returns directly rather than iterating over all instructions
		for (inst_iterator i = inst_begin(f), ie = inst_end(f); i != ie; ++i)
		{
			ReturnInst* ret = dyn_cast<ReturnInst>(&*i);

			if (ret)
			{
				assert(ret->getNumOperands() >= 1);
				Value& shadow = GetShadow(*ret->getOperand(0));
				CastInst* cast = CastInst::Create(Instruction::BitCast, &returnGlobal, shadow.getType()->getPointerTo(), "", ret);
				MarkAsInstrumented(*cast);
				StoreInst* store = new StoreInst(&shadow, cast, ret);
				MarkAsInstrumented(*store);

			}
		}
	}
	GlobalVariable& GetReturnGlobal(Type& type)
	{
		static GlobalVariable* returnGlobal = 0;

		unsigned size = GetAllocSize(type);
		Type* iNType = Type::getIntNTy(*context, size)->getPointerTo();

		if (returnGlobal != 0)
		{
			unsigned oldSize = GetAllocSize(type);

			if (size > oldSize)
			{
				returnGlobal->mutateType(iNType->getPointerTo());
				returnGlobal->setInitializer(&GetNullValue(*iNType));
			}

			return *returnGlobal;
		}

		returnGlobal =  new GlobalVariable(*module, iNType, false, GlobalVariable::CommonLinkage, &GetNullValue(*iNType), "");
		return *returnGlobal;
	}
	void AddShadow(Instruction& i, Value& shadow)
	{
		AddMetadata(i, "shadow", &shadow);
	}

	Value& GetShadow(Value& value)
	{

		Instruction* i;
		GlobalVariable* gv;
		Constant* c;
		Argument* param;

		if (! dumb) 
		{
			if ((i = dyn_cast<Instruction>(&value)))
			{
				if (! HasMetadata(*i, "dirty")) return GetNullValue(*value.getType());
			}
			else
			{
				if (dirtyValues.find(&value) == dirtyValues.end()) return GetNullValue(*value.getType());
			}
		}

		if ((i = dyn_cast<Instruction>(&value)))
		{
			if (AlreadyInstrumented(*i))
			{
				return *GetMetadata(*i, "shadow");
			}

			Instrument(*i);
			return GetShadow(*i);
		}
		else if ((gv = dyn_cast<GlobalVariable>(&value)))
		{
			return GetAllOnesValue(*gv->getType());
		}
		else if ((param = dyn_cast<Argument>(&value)))
		{
			static std::map<Argument*, Value*> handledParams;

			std::map<Argument*, Value*>::iterator it = handledParams.find(param);
			if (it != handledParams.end())
			{
				return *it->second;
			}

			GlobalVariable& paramGV = GetParamGlobal(*param->getType(), param->getArgNo());
			Function* f = param->getParent();
			assert(f != 0);
			BasicBlock& b = f->getEntryBlock();
			Instruction* firstInstruction = b.getFirstNonPHI();
			CastInst* cast = CastInst::Create(Instruction::BitCast, &paramGV, param->getType()->getPointerTo(), "", firstInstruction);
			MarkAsInstrumented(*cast);
			assert(cast != 0);
			Instruction* shadow = new LoadInst(cast, "", firstInstruction);
			MarkAsInstrumented(*shadow);
			assert(shadow != 0);
			handledParams.insert(std::pair<Argument*, Value*>(param, shadow));
			callsToBeHandled.insert(std::pair<Function*, Argument*>(f, param));
			return *shadow;
		}
		else if ((c = dyn_cast<Constant>(&value)))
		{
			if (c->getType()->isPointerTy())
			{
				return GetAllOnesValue(*c->getType());
			}
			else if (dyn_cast<ConstantExpr>(&value))
			{
				return GetAllOnesValue(*c->getType());
			}
			else
			{
				return GetNullValue(*c->getType());
			}
		}

		assert(false && "Shouldn't reach here");
		exit(1);
	}
	void InstrumentDelayedPHINodes()
	{
		std::map<PHINode*, PHINode*>::iterator it = delayedPHINodes.begin();

		while (it != delayedPHINodes.end())
		{
			PHINode& original = *it->first;
			PHINode& phiShadow = *it->second;

			int i = 0;
			PHINode::block_iterator blockIt = original.block_begin(), blockItEnd = original.block_end();
			for (PHINode::op_iterator it = original.op_begin(), itEnd = original.op_end(); it != itEnd; ++it)
			{
				Value& shadow = GetShadow(**it);

				Instruction* instruction = dyn_cast<Instruction>(&shadow);
				phiShadow.addIncoming(&shadow, *blockIt);
				++blockIt;
				i++;
			}

			delayedPHINodes.erase(it++);
		}
	}
	void HandleParamPassingTo(Function& f, Argument& param)
	{
		GlobalVariable& gv = GetParamGlobal(*param.getType(), param.getArgNo());

		for (Value::use_iterator it = f.use_begin(), itEnd = f.use_end(); it != itEnd; it++)
		{
			if (dyn_cast<CallInst>(*it) == 0 && dyn_cast<InvokeInst>(*it) == 0) continue;
			CallSite cs(*it);

			if (cs.getCalledFunction() != &f) continue;

			Value* arg = cs.getArgument(param.getArgNo());
			Value& shadow = GetShadow(*arg);
			CastInst* cast = CastInst::Create(Instruction::BitCast, &gv, arg->getType()->getPointerTo(), "", cs.getInstruction());
			MarkAsInstrumented(*cast);
			StoreInst* store = new StoreInst(&shadow, cast, cs.getInstruction()); //TODO: isn't there something like StoreInst::Create?
			MarkAsInstrumented(*store);
		}
	}
	Value* HandleExternFunctionCall(CallSite& cs)
	{
		Function& func = *cs.getCalledFunction();
		Type* retType = func.getReturnType();

		if (retType->isVoidTy()) return 0;
		if (retType->isPointerTy()) return &GetAllOnesValue(*retType);
		else return &GetNullValue(*retType);
	}

	Instruction* GetNextInstruction(Instruction& i)
	{
		BasicBlock::iterator it(&i);

		if (TerminatorInst *ti = dyn_cast<TerminatorInst>(it))
		{
			if (InvokeInst *ii = dyn_cast<InvokeInst>(ti))
			{
				BasicBlock *bb = ii->getNormalDest();
				return bb->getFirstInsertionPt();
			}
		}

		it++;

		return it;
	}

	virtual void getAnalysisUsage(AnalysisUsage &info) const
	{
		if (! dumb) 
		{
			info.addRequired<AddrLeaks>();
		}
	}

	bool AlreadyInstrumented(Instruction& i)
	{
		return HasMetadata(i, "instrumented");
	}

	void AddMetadata(Instruction& i, std::string tag, Value* v = 0)
	{
		std::vector<Value*> vals;

		if (v)
		{
			vals.push_back(v);
		}

		MDNode* node;
		node = MDNode::get(*context, vals);
		i.setMetadata(tag, node);
	}

	bool HasMetadata(Instruction& i, std::string tag)
	{
		MDNode* node = i.getMetadata(tag);
		return node != 0;
	}

	Value* GetMetadata(Instruction& i, std::string tag)
	{
		MDNode* node = i.getMetadata(tag);
		assert(node != 0);
		return node->getOperand(0);
	}

	void MarkAsInstrumented(Instruction& i)
	{
		AddMetadata(i, "instrumented");
	}
};

char Optimized::ID = 0;
static RegisterPass<Optimized> Y("leak", "Runtime leak checker", false, false);
