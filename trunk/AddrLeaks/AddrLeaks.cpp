/* Address Leak Analysis
 * 
 * This analysis is:
 *  - inter-procedural
 *  - context-sensitive
 *  - field-sensitive
 *
 * Universidade Federal de Minas Gerais - UFMG
 * Laboratório de Linguagens de Programação - LLP
 * Authors: Gabriel Quadros Silva
 *          Leonardo Vilela Teixeira
 */

#define DEBUG_TYPE "addrleaks"

#include <sys/time.h>
#include <sys/resource.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <iomanip>

#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/InstrTypes.h"
#include "llvm/Pass.h"
#include "llvm/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DebugInfo.h"

#include "PointerAnalysis.h"

typedef std::set<int> IntSet;
typedef std::map<int, IntSet> IntSetMap;
typedef std::map<int, int> IntMap;

using namespace llvm;

STATISTIC(leaksFound, "Number of address leaks found");
STATISTIC(totalPrintfs, "Number of printfs");
STATISTIC(totalLeakingPrintfs, "Number of leaking printfs");
STATISTIC(graphSize, "Size of graph");
STATISTIC(buggyPathsSize, "Size of buggy paths");

namespace {
    // AddrLeaks - The inter-procedural address leak analysis
    
    struct AddrLeaks : public ModulePass {
        enum nodeType {ADDR, VALUE};
        typedef std::pair<Value*, nodeType> VNode;
        typedef std::pair<int, nodeType> INode;
        typedef std::set<VNode> VNodeSet;
        typedef std::set<INode> INodeSet;

        static char ID;
        AddrLeaks() : ModulePass(ID) { 
            nextMemoryBlock = 1;
        }
        
        bool runOnModule(Module &M);

      private:
        int nextMemoryBlock;

        VNodeSet sources;
        INodeSet sources2;
        std::set<Value*> sinks;
        
        VNodeSet visited;
        INodeSet visited2;

        VNodeSet visited3;
        INodeSet visited4;

        VNodeSet vertices1;
        INodeSet vertices2;

        std::map<VNode, VNodeSet> memoryGraphVV;
        std::map<VNode, INodeSet> memoryGraphVi;
        std::map<INode, INodeSet> memoryGraphii;
        std::map<INode, VNodeSet> memoryGraphiV;

        std::map<VNode, VNodeSet> graphVV;
        std::map<VNode, INodeSet> graphVi;
        std::map<INode, VNodeSet> graphiV;

        std::set<Value*> leakedValues;
        
        std::map<Value*, std::vector<Value*> > phiValues;
        std::map<Value*, std::vector<int> > memoryBlock;
        std::map<int, std::vector<int> > memoryBlock2;
        std::map<Value*, std::vector<std::vector<int> > > memoryBlocks;
        std::map<Value*, Function*> function;

        PointerAnalysis *pointerAnalysis;
        std::map<Value*, int> value2int;
        std::map<int, Value*> int2value;

        int Value2Int(Value* v);
        bool dfs(Value* v, nodeType t);
        bool dfs(int v, nodeType t);
        int countBuggyPathSize(Value* v, nodeType t);
        int countBuggyPathSize(int v, nodeType t);
        void printDot(std::string moduleName);
        void matchFormalWithActualParameters(Function &F);
        void matchFormalWithActualParameters2(Function &F);
        void matchReturnValueWithReturnVariable(Function &F);
        void matchReturnValueWithReturnVariable2(Function &F);
        void addConstraints(Function &F);
        void buildMemoryGraph();
        void buildMyGraph(Function &F);
        std::string nodeType2String(nodeType t);
        void setFunction(Function &F, Value *v); 
        int getNewMemoryBlock();
        int getNewInt();
        void printInt2ValueTable();
        void handleAlloca(Instruction *I); 
        void handleNestedStructs(const Type *StTy, int parent);
        std::set<Value*> getLeakedValues();
    };
}

int AddrLeaks::getNewMemoryBlock() {
    return nextMemoryBlock++;
}

int AddrLeaks::getNewInt() {
    return getNewMemoryBlock();
}

std::string AddrLeaks::nodeType2String(nodeType t) {
    return t == ADDR ? "ADDR" : "VALUE";
}

////////////////////

bool AddrLeaks::dfs(int v, nodeType t) {
    if (visited2.find(std::make_pair(v, t)) != visited2.end())
        return false;

    visited2.insert(std::make_pair(v, t));
    
    if (sources2.find(std::make_pair(v, t)) != sources2.end())
        return true;
    
    for (VNodeSet::const_iterator it = graphiV[std::make_pair(v, t)].begin(); 
            it != graphiV[std::make_pair(v, t)].end(); ++it) {
        Value *vv = (*it).first;
        nodeType tt = (*it).second;

        if (dfs(vv, tt)) {
            sources2.insert(std::make_pair(v, t));
            return true;
        }
    }

    return false;
}

bool AddrLeaks::dfs(Value* v, nodeType t) {
    if (visited.find(std::make_pair(v, t)) != visited.end())
        return false;

    visited.insert(std::make_pair(v, t));

    if (sources.find(std::make_pair(v, t)) != sources.end()) {
        leakedValues.insert(v);
        return true;
    }
    
    for (VNodeSet::const_iterator it = graphVV[std::make_pair(v, t)].begin(); 
            it != graphVV[std::make_pair(v, t)].end(); ++it) {
        Value *vv = (*it).first;
        nodeType tt = (*it).second;

        if (dfs(vv, tt)) {
            leakedValues.insert(v);
            sources.insert(std::make_pair(v, t));
            return true;
        }
    }

    for (INodeSet::const_iterator it = graphVi[std::make_pair(v, t)].begin(); 
            it != graphVi[std::make_pair(v, t)].end(); ++it) {
        int vv = (*it).first;
        nodeType tt = (*it).second;

        if (dfs(vv, tt)) {
            leakedValues.insert(v);
            sources.insert(std::make_pair(v, t));
            return true;
        }
    }

    return false;
}

////////////////////

int AddrLeaks::countBuggyPathSize(int v, nodeType t) {
    int total = 0;
    
    if (visited4.find(std::make_pair(v, t)) != visited4.end())
        return 0;

    visited4.insert(std::make_pair(v, t));
    
    if (sources2.find(std::make_pair(v, t)) != sources2.end())
        return 1;
    
    for (VNodeSet::const_iterator it = graphiV[std::make_pair(v, t)].begin(); 
            it != graphiV[std::make_pair(v, t)].end(); ++it) {
        Value *vv = (*it).first;
        nodeType tt = (*it).second;

        total += countBuggyPathSize(vv, tt);
    }

    return total + 1;
}

int AddrLeaks::countBuggyPathSize(Value* v, nodeType t) {
    int total = 0;
    
    if (visited3.find(std::make_pair(v, t)) != visited3.end())
        return 0;

    visited3.insert(std::make_pair(v, t));

    if (sources.find(std::make_pair(v, t)) != sources.end())
        return 1;
    
    for (VNodeSet::const_iterator it = graphVV[std::make_pair(v, t)].begin(); 
            it != graphVV[std::make_pair(v, t)].end(); ++it) {
        Value *vv = (*it).first;
        nodeType tt = (*it).second;

        total += countBuggyPathSize(vv, tt);
    }

    for (INodeSet::const_iterator it = graphVi[std::make_pair(v, t)].begin(); 
            it != graphVi[std::make_pair(v, t)].end(); ++it) {
        int vv = (*it).first;
        nodeType tt = (*it).second;

        total += countBuggyPathSize(vv, tt);
    }

    return total + 1;
}

////////////////////

void AddrLeaks::printDot(std::string moduleName) {
    std::string errorInfo;
    std::string fileName = moduleName + ".dot";            

    raw_fd_ostream out(fileName.c_str(), errorInfo);
    out << "digraph " << moduleName << " {\n";

    // Print memory graph

    for (std::map<VNode, INodeSet>::const_iterator it = memoryGraphVi.begin();
            it != memoryGraphVi.end(); ++it) {
        Value *src = it->first.first;
        nodeType srcType = it->first.second;
        Function *f = function[src];
        std::string srcFunctionName = f ? f->getName().str() : "global";
 
        for (INodeSet::const_iterator it2 = it->second.begin(); it2 != it->second.end();
                ++it2) {
            int dst = (*it2).first;
            nodeType dstType = (*it2).second;
            std::string dstFunctionName = "memory";
            
            if (!src->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << src->getName() << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << "m" << dst << " " << nodeType2String(dstType) << "\" [style=dashed]\n";
            else if (src->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << *src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << "m" << dst << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
        }
    }

    for (std::map<INode, INodeSet>::const_iterator it = memoryGraphii.begin();
            it != memoryGraphii.end(); ++it) {
        int src = it->first.first;
        nodeType srcType = it->first.second;
        std::string srcFunctionName = "memory";
 
        for (INodeSet::const_iterator it2 = it->second.begin(); it2 != it->second.end();
                ++it2) {
            int dst = (*it2).first;
            nodeType dstType = (*it2).second;
            std::string dstFunctionName = "memory";
            
            out << "    \"(" << srcFunctionName << ") " << "m" << src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                   dstFunctionName << ") " << "m" << dst << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
        }
    }

    for (std::map<INode, VNodeSet>::const_iterator it = memoryGraphiV.begin();
            it != memoryGraphiV.end(); ++it) {
        int src = it->first.first;
        nodeType srcType = it->first.second;
        std::string srcFunctionName = "memory";
 
        for (VNodeSet::const_iterator it2 = it->second.begin(); it2 != it->second.end();
                ++it2) {
            Value *dst = (*it2).first;
            nodeType dstType = (*it2).second;
            Function *f = function[dst];
            std::string dstFunctionName = f ? f->getName().str() : "global";
            
            if (!dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << "m" << src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << dst->getName() << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
            else
                out << "    \"(" << srcFunctionName << ") " << "m" << src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << *dst << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
        }
    }

    for (std::map<VNode, VNodeSet>::const_iterator it = memoryGraphVV.begin();
            it != memoryGraphVV.end(); ++it) {
        Value *src = it->first.first;
        nodeType srcType = it->first.second;
        Function *f = function[src];
        std::string srcFunctionName = f ? f->getName().str() : "global";
 
        for (VNodeSet::const_iterator it2 = it->second.begin(); it2 != it->second.end();
                ++it2) {
            Value *dst = (*it2).first;
            nodeType dstType = (*it2).second;
            Function *f = function[dst];
            std::string dstFunctionName = f ? f->getName().str() : "global";
            
            if (src->getName().empty() && dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << *src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << *dst << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
            else if (!src->getName().empty() && !dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << src->getName() << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << dst->getName() << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
            else if (!src->getName().empty() && dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << src->getName() << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << *dst << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
            else
                out << "    \"(" << srcFunctionName << ") " << *src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << dst->getName() << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
        }
    }

    out << "\n";

    // Print my graph

    for (std::map<VNode, VNodeSet>::const_iterator it = graphVV.begin();
            it != graphVV.end(); ++it) {
        Value *src = it->first.first;
        nodeType srcType = it->first.second;
        Function *f = function[src];
        std::string srcFunctionName = f ? f->getName().str() : "global";
 
        for (VNodeSet::const_iterator it2 = it->second.begin(); it2 != it->second.end();
                ++it2) {
            Value *dst = (*it2).first;
            nodeType dstType = (*it2).second;
            Function *f = function[dst];
            std::string dstFunctionName = f ? f->getName().str() : "global";
            
            if (src->getName().empty() && dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << *src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << *dst << " " << nodeType2String(dstType) << "\"\n"; 
            else if (!src->getName().empty() && !dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << src->getName() << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << dst->getName() << " " << nodeType2String(dstType) << "\"\n"; 
            else if (!src->getName().empty() && dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << src->getName() << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << *dst << " " << nodeType2String(dstType) << "\"\n"; 
            else
                out << "    \"(" << srcFunctionName << ") " << *src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << dst->getName() << " " << nodeType2String(dstType) << "\"\n"; 
        }
    }

    for (std::map<VNode, INodeSet>::const_iterator it = graphVi.begin();
            it != graphVi.end(); ++it) {
        Value *src = it->first.first;
        nodeType srcType = it->first.second;
        Function *f = function[src];
        std::string srcFunctionName = f ? f->getName().str() : "global";
 
        for (INodeSet::const_iterator it2 = it->second.begin(); it2 != it->second.end();
                ++it2) {
            int dst = (*it2).first;
            nodeType dstType = (*it2).second;
            std::string dstFunctionName = "memory";
            
            if (!src->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << src->getName() << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << "m" << dst << " " << nodeType2String(dstType) << "\"\n";
            else if (src->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << *src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << "m" << dst << " " << nodeType2String(dstType) << "\"\n"; 
        }
    }

    for (std::map<INode, VNodeSet>::const_iterator it = graphiV.begin();
            it != graphiV.end(); ++it) {
        int src = it->first.first;
        nodeType srcType = it->first.second;
        std::string srcFunctionName = "memory";
 
        for (VNodeSet::const_iterator it2 = it->second.begin(); it2 != it->second.end();
                ++it2) {
            Value *dst = (*it2).first;
            nodeType dstType = (*it2).second;
            Function *f = function[dst];
            std::string dstFunctionName = f ? f->getName().str() : "global";
            
            if (!dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << "m" << src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << dst->getName() << " " << nodeType2String(dstType) << "\"\n";
            else if (dst->getName().empty())
                out << "    \"(" << srcFunctionName << ") " << "m" << src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                       dstFunctionName << ") " << *dst << " " << nodeType2String(dstType) << "\"\n"; 
        }
    }

    out << "}\n\n";
    out.close();
}

void AddrLeaks::matchFormalWithActualParameters(Function &F) {
    if (F.arg_empty() || F.use_empty()) return;

    for (Value::use_iterator UI = F.use_begin(), E = F.use_end(); UI != E; ++UI) {
        User *U = *UI;
        
        if (isa<BlockAddress>(U)) continue;
        if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) return;

        CallSite CS(cast<Instruction>(U));
        if (!CS.isCallee(UI))
            return;

        CallSite::arg_iterator actualArgIter = CS.arg_begin();
        Function::arg_iterator formalArgIter = F.arg_begin();
        int size = F.arg_size();

        for (int i = 0; i < size; ++i, ++actualArgIter, ++formalArgIter) {
            Value *actualArg = *actualArgIter;
            Value *formalArg = formalArgIter;

            int a = Value2Int(formalArg);
            int b = Value2Int(actualArg);
            pointerAnalysis->addBase(a, b);
        }
    }
}


int AddrLeaks::Value2Int(Value *v) {
    int n;

    if (value2int.count(v))
        return value2int[v];
    
    n = getNewInt();
    value2int[v] = n;
    int2value[n] = v;

    return n;
}

void AddrLeaks::matchReturnValueWithReturnVariable(Function &F) {
    if (F.getReturnType()->isVoidTy() || F.mayBeOverridden()) return;

    std::set<Value*> retVals;

    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
        if (ReturnInst *RI = dyn_cast<ReturnInst>(BB->getTerminator())) {
            Value *v = RI->getOperand(0);

            retVals.insert(v);
        }
    }

    for (Value::use_iterator UI = F.use_begin(), E = F.use_end(); UI != E; ++UI) {
        CallSite CS(*UI);
        Instruction *Call = CS.getInstruction();

        if (!Call || !CS.isCallee(UI)) continue;

        if (Call->use_empty()) continue;

        for (std::set<Value*>::iterator it = retVals.begin(), E = retVals.end(); it != E; ++it) {
            
            int a = Value2Int(CS.getCalledFunction());
            int b = Value2Int(*it);
            pointerAnalysis->addBase(a, b);
        }
    }
}

void AddrLeaks::matchFormalWithActualParameters2(Function &F) {
    if (F.arg_empty() || F.use_empty()) return;

    for (Value::use_iterator UI = F.use_begin(), E = F.use_end(); UI != E; ++UI) {
        User *U = *UI;
        
        if (isa<BlockAddress>(U)) continue;
        if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) return;

        CallSite CS(cast<Instruction>(U));
        if (!CS.isCallee(UI))
            return;

        CallSite::arg_iterator actualArgIter = CS.arg_begin();
        Function::arg_iterator formalArgIter = F.arg_begin();
        int size = F.arg_size();

        for (int i = 0; i < size; ++i, ++actualArgIter, ++formalArgIter) {
            Value *actualArg = *actualArgIter;
            Value *formalArg = formalArgIter;

            graphVV[std::make_pair(formalArg, VALUE)].insert(std::make_pair(actualArg, VALUE));
        }
    }
}

void AddrLeaks::matchReturnValueWithReturnVariable2(Function &F) {
    if (F.getReturnType()->isVoidTy() || F.mayBeOverridden()) return;

    std::set<Value*> retVals;

    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
        if (ReturnInst *RI = dyn_cast<ReturnInst>(BB->getTerminator())) {
            Value *v = RI->getOperand(0);

            retVals.insert(v);
        }
    }

    for (Value::use_iterator UI = F.use_begin(), E = F.use_end(); UI != E; ++UI) {
        CallSite CS(*UI);
        Instruction *Call = CS.getInstruction();

        if (!Call || !CS.isCallee(UI)) continue;

        if (Call->use_empty()) continue;

        for (std::set<Value*>::iterator it = retVals.begin(), E = retVals.end(); it != E; ++it) {
            graphVV[std::make_pair(CS.getCalledFunction(), VALUE)].insert(std::make_pair(*it, VALUE));
        }
    }
}

void AddrLeaks::printInt2ValueTable() {
    errs() << "### Int2Value Table ###\n";
    
    for (int i = 1; i < nextMemoryBlock; i++) {
        if (int2value.count(i))
            errs() << i << "\t" << *int2value[i] << "\n";
        else
            errs() << i << "\t" << "m" << i << "\n";
    }

    errs() << "######################\n";
}

bool AddrLeaks::runOnModule(Module &M) {
    struct timeval startTime, endTime;
    struct rusage ru;

    getrusage(RUSAGE_SELF, &ru);
    startTime = ru.ru_utime;

    // Start pointer analysis

    pointerAnalysis = new PointerAnalysis();
    
    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        if (!F->isDeclaration()) {
            addConstraints(*F);
            matchFormalWithActualParameters(*F);
            matchReturnValueWithReturnVariable(*F);
        }
    }

    pointerAnalysis->solve();

    // End pointer analysis

    getrusage(RUSAGE_SELF, &ru);
    endTime = ru.ru_utime;
    
    double tS = startTime.tv_sec + (double)startTime.tv_usec / 1000000;
    double tE = endTime.tv_sec + (double)endTime.tv_usec / 1000000;
    double deltaTime = tE - tS;

    std::stringstream ss(std::stringstream::in | std::stringstream::out);
    ss << std::fixed << std::setprecision(4) << deltaTime;
    std::string deltaTimeStr;
    ss >> deltaTimeStr;

    errs() << deltaTimeStr << " Time to perform the pointer analysis\n";

    //pointerAnalysis->print();

    //printInt2ValueTable();

    // Build memory graph from pointsTo sets

    buildMemoryGraph();

    // Build my graph

    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        if (!F->isDeclaration()) {
            buildMyGraph(*F);
            matchFormalWithActualParameters2(*F);
            matchReturnValueWithReturnVariable2(*F);
        }
    }

    graphSize = vertices1.size() + vertices2.size();

    printDot("module");
    
    // Detect leaks via printf
    Function *sink = M.getFunction("printf");

    if (sink) {
        for (Value::use_iterator I = sink->use_begin(), E = sink->use_end(); I != E; ++I) {
            if (Instruction *use = dyn_cast<Instruction>(*I)) {
                CallSite CS(use);
                std::vector<Value*> leaked;
                
                sinks.insert(use);
                totalPrintfs++;
                
                CallSite::arg_iterator AI = CS.arg_begin();
            
                std::vector<bool> vaza, isString(false);
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

                if (!formatString.empty()) {
                    int formatStringSize = formatString.size();
                    for (int i = 0; i < formatStringSize - 1; i++) {
                        if (formatString[i] == '%') {
                            if (formatString[i + 1] == 'c' ||
                                formatString[i + 1] == 'e' ||
                                formatString[i + 1] == 'E' ||
                                formatString[i + 1] == 'f' ||
                                formatString[i + 1] == 'g' ||
                                formatString[i + 1] == 'G' ||
                                formatString[i + 1] == 'n' ||
                                formatString[i + 1] == 'L' ||
                                formatString[i + 1] == '%') {
                                vaza.push_back(false);
                                i++;
                            } else if (formatString[i + 1] == 'd' ||
                                       formatString[i + 1] == 'i' ||
                                       formatString[i + 1] == 'o' ||
                                       formatString[i + 1] == 'u' ||
                                       formatString[i + 1] == 'x' ||
                                       formatString[i + 1] == 'X' ||
                                       formatString[i + 1] == 'p' ||
                                       formatString[i + 1] == 'h' ||
                                       formatString[i + 1] == 'l') {
                                vaza.push_back(true);
                                i++;
                            } else if (formatString[i + 1] == 's') { // Special case
                                vaza.push_back(true);
                                isString[vaza.size() - 1] = true;
                                i++;
                            }
                        }
                    }
                }

                AI++;
    
                if (!hasFormat || !vaza.empty()) {
                    int vazaSize = vaza.size();
                    
                    for (int i = 0; AI != CS.arg_end(); ++AI, ++i) {
                        Value *v = *AI;
                       
                        if (i < vazaSize && vaza[i]) {
                            if (isString[i]) { // Handle special string case.
                                IntSet pointsTo = pointerAnalysis->pointsTo(Value2Int(v));
                            
                                for (IntSet::const_iterator it = pointsTo.begin(), 
                                    e = pointsTo.end(); it != e; it++) {
                                    
                                    visited.clear();
                                    visited2.clear();

                                    if (int2value.count(*it)) {
                                        Value *vv = int2value[*it];

                                        if (dfs(vv, VALUE)) {
                                            leaked.push_back(vv);
                                            leaksFound++;
                                        }
                                    } else {
                                        if (dfs(*it, VALUE)) {
                                            leaked.push_back(v); // FIX
                                            leaksFound++;
                                        }
                                    }
                                }
                            } else {
                                // Just to collect some statistics. Will disappear in future
                                // buggyPathsSize += countBuggyPathSize(v, VALUE);

                                // Real search
                                visited.clear();
                                visited2.clear();
                                
                                if (dfs(v, VALUE)) {
                                    leaked.push_back(v);
                                    leaksFound++;
                                }
                            }
                        }
                    }
                }

                unsigned leakedSize = leaked.size();

                if (leakedSize > 0) {
                    totalLeakingPrintfs++;
                    errs() << "=========================================\n";
                    errs() << "Pointer leak detected in the instruction:\n";
                    errs() << *use << "\n\n";
                    errs() << "The leaked addresses are:\n";
                    
                    for (unsigned i = 0; i < leakedSize; ++i) {
                        Function *f = function[leaked[i]];
                        std::string functionName = f ? f->getName().str() : "undefined";

                        if (leaked[i]->getName().empty())
                            errs() << " - (" << functionName << ") " << *leaked[i] << " = " << leaked[i] << "\n";
                        else
                            errs() << " - (" << functionName << ") " << leaked[i]->getName() << " = " << leaked[i] << "\n";
                    }

                    if (MDNode *N = use->getMetadata("dbg")) {
                        DILocation Loc(N);
                        unsigned Line = Loc.getLineNumber();
                        StringRef File = Loc.getFilename();
                        StringRef Dir = Loc.getDirectory();
                        errs() << "Dir: " << Dir << ", File: " << File << ", Line: " << Line << "\n";
                    }

                    errs() << "=========================================\n";
                    errs() << "\n";
                }
            }
        }
    }

    /* Static analysis completed.
     * Call the method getLeakedValues() to get the instructions to instrument */

    std::set<Value*> lv = getLeakedValues();

    for (std::set<Value*>::iterator it = lv.begin(); it != lv.end(); it++) {
        errs() << "Leaked Value: " << **it << "\n";
    }

    return false;
}

std::set<Value*> AddrLeaks::getLeakedValues() {
    return leakedValues;
}

void AddrLeaks::setFunction(Function &F, Value *v) {
    // Set the associated function to NULL when its previous value is different of the current one
    if (function.count(v) && function[v] != &F)
        function[v] = 0;
    else
        function[v] = &F;
}

void AddrLeaks::handleAlloca(Instruction *I) {
    AllocaInst *AI = dyn_cast<AllocaInst>(I);
    const Type *Ty = AI->getAllocatedType();
    
    std::vector<int> mems;
    unsigned numElems = 1;
    bool isStruct = false;

    if (Ty->isStructTy()) { // Handle structs
        const StructType *StTy = dyn_cast<StructType>(Ty);
        numElems = StTy->getNumElements();
        isStruct = true;
    }

    if (!memoryBlock.count(I)) {
        for (unsigned i = 0; i < numElems; i++) {
            mems.push_back(getNewMemoryBlock());

            if (isStruct) {
                const StructType *StTy = dyn_cast<StructType>(Ty);
                
                if (StTy->getElementType(i)->isPointerTy())
                    sources2.insert(std::make_pair(mems[i], VALUE));
                else if (StTy->getElementType(i)->isStructTy())
                    handleNestedStructs(StTy->getElementType(i), mems[i]);
            }
        }

        memoryBlock[I] = mems;
    } else {
        mems = memoryBlock[I];
    }

    for (unsigned i = 0; i < mems.size(); i++) {
        int a = Value2Int(I);
        pointerAnalysis->addAddr(a, mems[i]);
        sources2.insert(std::make_pair(mems[i], ADDR));
    }
}

void AddrLeaks::handleNestedStructs(const Type *Ty, int parent) {
    const StructType *StTy = dyn_cast<StructType>(Ty);
    unsigned numElems = StTy->getNumElements();
    std::vector<int> mems;

    for (unsigned i = 0; i < numElems; i++) {
        mems.push_back(getNewMemoryBlock());

        if (StTy->getElementType(i)->isPointerTy())
            sources2.insert(std::make_pair(mems[i], VALUE));
        else if (StTy->getElementType(i)->isStructTy())
            handleNestedStructs(StTy->getElementType(i), mems[i]);
    }

    memoryBlock2[parent] = mems;

    for (unsigned i = 0; i < mems.size(); i++) {
        pointerAnalysis->addAddr(parent, mems[i]);
        sources2.insert(std::make_pair(mems[i], ADDR));
    }
}

void AddrLeaks::addConstraints(Function &F) {
    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
        for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
            if (isa<PHINode>(I)) {
                PHINode *Phi = dyn_cast<PHINode>(I);
                const Type *Ty = Phi->getType();

                if (Ty->isPointerTy()) {
                    unsigned n = Phi->getNumIncomingValues();
                    std::vector<Value*> values;
                    
                    for (unsigned i = 0; i < n; i++) {
                        Value *v = Phi->getIncomingValue(i);

                        values.push_back(v);
                    }

                    phiValues[I] = values;
                }
            }
        }
    } 
    
    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
        for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
            if (isa<CallInst>(I)) {
                CallInst *CI = dyn_cast<CallInst>(I);

                if (CI) {
                    Function *FF = CI->getCalledFunction();

                    if (FF && (FF->getName() == "malloc" || FF->getName() == "realloc" || 
                               FF->getName() == "calloc")) {
                        std::vector<int> mems;

                        if (!memoryBlock.count(I)) {
                            mems.push_back(getNewMemoryBlock());
                            memoryBlock[I] = mems;
                        } else {
                            mems = memoryBlock[I];
                        }

                        int a = Value2Int(I);
                        pointerAnalysis->addAddr(a, mems[0]);
                        sources2.insert(std::make_pair(mems[0], ADDR));
                    }
                }
            }

            // Handle special operations
            switch (I->getOpcode()) {
                case Instruction::Alloca:
                {
                    handleAlloca(I);
                    
                    break;
                }
                case Instruction::GetElementPtr:
                {
                    GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(I);
                    Value *v = GEPI->getPointerOperand();
                    const PointerType *PoTy = GEPI->getPointerOperandType();
                    const Type *Ty = PoTy->getElementType();

                    if (Ty->isStructTy()) {
                        if (phiValues.count(v)) {
                            std::vector<Value*> values = phiValues[v];

                            for (unsigned i = 0; i < values.size(); i++) {
                                Value* vv = values[i];

                                if (memoryBlocks.count(vv)) {
                                    for (unsigned j = 0; j < memoryBlocks[vv].size(); j++) {
                                        int i = 0;
                                        unsigned pos = 0;

                                        for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                            if (i == 1)
                                                pos = cast<ConstantInt>(*it)->getZExtValue();
                                            
                                            i++;
                                        }
                                        std::vector<int> mems = memoryBlocks[vv][j];
                                        int a = Value2Int(I);
                                        pointerAnalysis->addAddr(a, mems[pos]);
                                    }
                                } else {
                                    if (memoryBlock.count(vv)) {
                                        if (isa<BitCastInst>(vv)) {
                                            BitCastInst *BC = dyn_cast<BitCastInst>(vv);

                                            Value *v2 = BC->getOperand(0);
                                            
                                            if (memoryBlock.count(v2)) {
                                                int i = 0;
                                                unsigned pos = 0;

                                                for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                                    if (i == 1)
                                                        pos = cast<ConstantInt>(*it)->getZExtValue();
                                                    
                                                    i++;
                                                }

                                                std::vector<int> mems = memoryBlock[v2];
                                                int parent = mems[0];
                                                std::vector<int> mems2 = memoryBlock2[parent];

                                                int a = Value2Int(I);
                                                pointerAnalysis->addAddr(a, mems2[pos]);
                                            }
                                        } else {
                                            int i = 0;
                                            unsigned pos = 0;

                                            for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                                if (i == 1)
                                                    pos = cast<ConstantInt>(*it)->getZExtValue();
                                                
                                                i++;
                                            }
                                            
                                            std::vector<int> mems = memoryBlock[vv];
                                            int a = Value2Int(I);
                                            //pointerAnalysis->addBase(a, mems[pos]);
                                            pointerAnalysis->addAddr(a, mems[pos]);
                                        }
                                    } else {
                                        GetElementPtrInst *GEPI2 = dyn_cast<GetElementPtrInst>(vv);
                                        
                                        if (!GEPI2)
                                            break;
                                        
                                        Value *v2 = GEPI2->getPointerOperand();

                                        if (memoryBlock.count(v2)) {
                                            int i = 0;
                                            unsigned pos = 0;

                                            for (User::op_iterator it = GEPI2->idx_begin(), e = GEPI2->idx_end(); it != e; ++it) {
                                                if (i == 1)
                                                    pos = cast<ConstantInt>(*it)->getZExtValue();
                                                
                                                i++;
                                            }

                                            std::vector<int> mems = memoryBlock[v2];
                                            int parent = mems[pos];
                                           
                                            i = 0;
                                            unsigned pos2 = 0;
                                            
                                            for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                                if (i == 1)
                                                    pos2 = cast<ConstantInt>(*it)->getZExtValue();
                                                
                                                i++;
                                            }

                                            std::vector<int> mems2 = memoryBlock2[parent];
                                            int a = Value2Int(I);
                                            pointerAnalysis->addAddr(a, mems2[pos2]);
                                            memoryBlock[v] = mems2;
                                        }
                                    }
                                }
                            }
                        } else {
                            if (memoryBlock.count(v)) {
                                if (isa<BitCastInst>(v)) {
                                    BitCastInst *BC = dyn_cast<BitCastInst>(v);

                                    Value *v2 = BC->getOperand(0);
                                    
                                    if (memoryBlock.count(v2)) {
                                        int i = 0;
                                        unsigned pos = 0;

                                        for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                            if (i == 1)
                                                pos = cast<ConstantInt>(*it)->getZExtValue();
                                            
                                            i++;
                                        }

                                        std::vector<int> mems = memoryBlock[v2];
                                        int parent = mems[0];
                                        std::vector<int> mems2 = memoryBlock2[parent];

                                        int a = Value2Int(I);
                                        pointerAnalysis->addAddr(a, mems2[pos]);
                                    }
                                } else {
                                    int i = 0;
                                    unsigned pos = 0;

                                    for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                        if (i == 1)
                                            pos = cast<ConstantInt>(*it)->getZExtValue();
                                        
                                        i++;
                                    }
                                    
                                    std::vector<int> mems = memoryBlock[v];
                                    int a = Value2Int(I);
                                    //pointerAnalysis->addBase(a, mems[pos]);
                                    pointerAnalysis->addAddr(a, mems[pos]);
                                }
                            } else {
                                GetElementPtrInst *GEPI2 = dyn_cast<GetElementPtrInst>(v);
                                
                                if (!GEPI2)
                                    break;
                                
                                Value *v2 = GEPI2->getPointerOperand();

                                if (memoryBlock.count(v2)) {
                                    int i = 0;
                                    unsigned pos = 0;

                                    for (User::op_iterator it = GEPI2->idx_begin(), e = GEPI2->idx_end(); it != e; ++it) {
                                        if (i == 1)
                                            pos = cast<ConstantInt>(*it)->getZExtValue();
                                        
                                        i++;
                                    }

                                    std::vector<int> mems = memoryBlock[v2];
                                    int parent = mems[pos];
                                   
                                    i = 0;
                                    unsigned pos2 = 0;
                                    
                                    for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                        if (i == 1)
                                            pos2 = cast<ConstantInt>(*it)->getZExtValue();
                                        
                                        i++;
                                    }

                                    std::vector<int> mems2 = memoryBlock2[parent];
                                    int a = Value2Int(I);
                                    pointerAnalysis->addAddr(a, mems2[pos2]);
                                    memoryBlock[v] = mems2;
                                }
                            }
                        }
                    } else {
                        int a = Value2Int(I);
                        int b = Value2Int(v);
                        pointerAnalysis->addBase(a, b);
                    }

                    sources.insert(std::make_pair(I, VALUE));
                    break;
                }
                case Instruction::BitCast:
                {
                    Value *src = I->getOperand(0);
                    Value *dst = I;

                    const Type *srcTy = src->getType();
                    const Type *dstTy = dst->getType();
                    
                    if (srcTy->isPointerTy()) {
                        if (dstTy->isPointerTy()) {
                            const PointerType *PoTy = cast<PointerType>(dstTy);
                            const Type *Ty = PoTy->getElementType();

                            if (Ty->isStructTy()) {
                                if (memoryBlock.count(src)) {
                                    std::vector<int> mems = memoryBlock[src];
                                    int parent = mems[0];
                               
                                    handleNestedStructs(Ty, parent);
                                    memoryBlock[I] = mems;
                                }
                            }
                        }

                        int a = Value2Int(I);
                        int b = Value2Int(src);
                        pointerAnalysis->addBase(a, b);
                    }

                    break;
                }
                case Instruction::Store:
                {
                    // *ptr = v
                    StoreInst *SI = dyn_cast<StoreInst>(I);
                    Value *v = SI->getValueOperand();
                    Value *ptr = SI->getPointerOperand();

                    if (v->getType()->isPointerTy()) {
                        int a = Value2Int(ptr);
                        int b = Value2Int(v);

                        pointerAnalysis->addStore(a, b);
                    }

                    break;
                }
                case Instruction::Load:
                {
                    // I = *ptr
                    LoadInst *LI = dyn_cast<LoadInst>(I);
                    Value *ptr = LI->getPointerOperand();

                    int a = Value2Int(I);
                    int b = Value2Int(ptr);
                    pointerAnalysis->addLoad(a, b);

                    break;
                }
                case Instruction::PHI:
                {
                    PHINode *Phi = dyn_cast<PHINode>(I);
                    const Type *Ty = Phi->getType();

                    if (Ty->isPointerTy()) {
                        unsigned n = Phi->getNumIncomingValues();
                        std::vector<Value*> values;
                        
                        for (unsigned i = 0; i < n; i++) {
                            Value *v = Phi->getIncomingValue(i);
                            int a = Value2Int(I);
                            int b = Value2Int(v);
                            pointerAnalysis->addBase(a, b);

                            values.push_back(v);

                            if (phiValues.count(v)) {
                                if (memoryBlocks.count(v)) {
                                    memoryBlocks[I] = std::vector<std::vector<int> >();
                                    memoryBlocks[I].insert(memoryBlocks[I].end(), memoryBlocks[v].begin(), memoryBlocks[v].end());
                                }
                            } else {
                                if (memoryBlock.count(v)) {
                                    memoryBlocks[I] = std::vector<std::vector<int> >();

                                    if (isa<BitCastInst>(v)) {
                                        BitCastInst *BC = dyn_cast<BitCastInst>(v);

                                        Value *v2 = BC->getOperand(0);
                                        
                                        if (memoryBlock.count(v2)) {
                                            std::vector<int> mems = memoryBlock[v2];
                                            int parent = mems[0];
                                            std::vector<int> mems2 = memoryBlock2[parent];

                                            memoryBlocks[I].push_back(mems2);
                                        }
                                    } else
                                        memoryBlocks[I].push_back(memoryBlock[v]);
                                }
                            }
                        }
                    }

                    break;
                }
            }
        }
    }
}

void AddrLeaks::buildMemoryGraph() {
    IntSetMap pointsTo = pointerAnalysis->allPointsTo();

    for (IntSetMap::const_iterator it = pointsTo.begin(), e = pointsTo.end(); it != e; it++) {
        int src = it->first;

        for (IntSet::const_iterator it2 = it->second.begin(), 
                e = it->second.end(); it2 != e; it2++) {
            int dst = *it2;

            if (int2value.count(src) && int2value.count(dst))
                memoryGraphVV[std::make_pair(int2value[src], VALUE)].insert(std::make_pair(int2value[dst], VALUE));
            else if (!int2value.count(src) && int2value.count(dst))
                memoryGraphiV[std::make_pair(src, VALUE)].insert(std::make_pair(int2value[dst], VALUE));
            else if (int2value.count(src) && !int2value.count(dst))
                memoryGraphVi[std::make_pair(int2value[src], VALUE)].insert(std::make_pair(dst, VALUE));
            else if (!int2value.count(src) && !int2value.count(dst))
                memoryGraphii[std::make_pair(src, VALUE)].insert(std::make_pair(dst, VALUE));
        }
    }
}

void AddrLeaks::buildMyGraph(Function &F) {
    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
        for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
            if (isa<CallInst>(I)) {
                CallInst *CI = dyn_cast<CallInst>(I);

                if (CI) {
                    Function *FF = CI->getCalledFunction();

                    if (FF && (FF->getName() == "malloc" || FF->getName() == "realloc" ||
                               FF->getName() == "calloc")) {
                        std::vector<int> mems;
                        
                        mems = memoryBlock[I];
                        
                        for (unsigned i = 0; i < mems.size(); i++) {
                            graphVi[std::make_pair(I, VALUE)].insert(std::make_pair(mems[i], ADDR));
                            vertices1.insert(std::make_pair(I, VALUE));
                            vertices2.insert(std::make_pair(mems[i], ADDR));
                        }
                    }
                }
            }
            
            // Handle special operations
            switch (I->getOpcode()) {
                case Instruction::Alloca:
                {
                    std::vector<int> mems;
                    
                    mems = memoryBlock[I];

                    for (unsigned i = 0; i < mems.size(); i++) {
                        graphVi[std::make_pair(I, VALUE)].insert(std::make_pair(mems[i], ADDR));
                        vertices1.insert(std::make_pair(I, VALUE));
                        vertices2.insert(std::make_pair(mems[i], ADDR));
                    }

                    setFunction(F, I);
                    
                    break;
                }
                case Instruction::GetElementPtr:
                {
                    GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(I);
                    Value *v = GEPI->getPointerOperand();
                    const PointerType *PoTy = GEPI->getPointerOperandType();
                    const Type *Ty = PoTy->getElementType();

                    if (Ty->isStructTy()){
                        if (memoryBlock.count(v)) {
                            if (isa<BitCastInst>(v)) {
                                BitCastInst *BC = dyn_cast<BitCastInst>(v);

                                Value *v2 = BC->getOperand(0);
                                
                                if (memoryBlock.count(v2)) {
                                    int i = 0;
                                    unsigned pos = 0;

                                    for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                        if (i == 1)
                                            pos = cast<ConstantInt>(*it)->getZExtValue();
                                        
                                        i++;
                                    }

                                    std::vector<int> mems = memoryBlock[v2];
                                    int parent = mems[0];
                                    std::vector<int> mems2 = memoryBlock2[parent];

                                    graphVi[std::make_pair(I, VALUE)].insert(std::make_pair(mems2[pos], ADDR));
                                    vertices1.insert(std::make_pair(I, VALUE));
                                    vertices2.insert(std::make_pair(mems2[pos], ADDR));
                                }
                            } else {
                                int i = 0;
                                unsigned pos = 0;

                                for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                    if (i == 1)
                                        pos = cast<ConstantInt>(*it)->getZExtValue();
                                    
                                    i++;
                                }
                                
                                std::vector<int> mems = memoryBlock[v];
                                
                                graphVi[std::make_pair(I, VALUE)].insert(std::make_pair(mems[pos], ADDR));
                                vertices1.insert(std::make_pair(I, VALUE));
                                vertices2.insert(std::make_pair(mems[pos], ADDR));
                            }
                        } else {
                            GetElementPtrInst *GEPI2 = dyn_cast<GetElementPtrInst>(v);
                            
                            if (!GEPI2)
                                break;
                            
                            Value *v2 = GEPI2->getPointerOperand();

                            if (memoryBlock.count(v2)) {
                                int i = 0;
                                unsigned pos = 0;

                                for (User::op_iterator it = GEPI2->idx_begin(), e = GEPI2->idx_end(); it != e; ++it) {
                                    if (i == 1)
                                        pos = cast<ConstantInt>(*it)->getZExtValue();
                                    
                                    i++;
                                }

                                std::vector<int> mems = memoryBlock[v2];
                                int parent = mems[pos];
                               
                                i = 0;
                                unsigned pos2 = 0;
                                
                                for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                    if (i == 1)
                                        pos2 = cast<ConstantInt>(*it)->getZExtValue();
                                    
                                    i++;
                                }

                                std::vector<int> mems2 = memoryBlock2[parent];
                            
                                graphVi[std::make_pair(I, VALUE)].insert(std::make_pair(mems2[pos2], ADDR));
                                vertices1.insert(std::make_pair(I, VALUE));
                                vertices2.insert(std::make_pair(mems2[pos2], ADDR));
                            }
                        }

                        /*
                        if (memoryBlock.count(v)) {
                            int i = 0;
                            unsigned pos = 0;

                            for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                                if (i == 1)
                                    pos = cast<ConstantInt>(*it)->getZExtValue();
                                
                                i++;
                            }

                            std::vector<int> mems = memoryBlock[v];

                            graphVi[std::make_pair(I, VALUE)].insert(std::make_pair(mems[pos], ADDR));
                            vertices1.insert(std::make_pair(I, VALUE));
                            vertices2.insert(std::make_pair(mems[pos], ADDR));
                        }
                        */
                    } else {
                        graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(v, ADDR));
                        vertices1.insert(std::make_pair(I, VALUE));
                        vertices1.insert(std::make_pair(v, ADDR));
                    }
                    
                    setFunction(F, I);
                    setFunction(F, v);
                    
                    break;
                }
                case Instruction::PtrToInt:
                {
                    Value *v = I->getOperand(0);
                    
                    graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(v, VALUE));
                    vertices1.insert(std::make_pair(I, VALUE));
                    vertices1.insert(std::make_pair(v, VALUE));
                    
                    setFunction(F, I);
                    setFunction(F, v);
                    
                    break;
                }
                case Instruction::IntToPtr:
                {
                    Value *v = I->getOperand(0);
                    
                    graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(v, VALUE));
                    vertices1.insert(std::make_pair(I, VALUE));
                    vertices1.insert(std::make_pair(v, VALUE));
                    
                    sources.insert(std::make_pair(I, VALUE));

                    setFunction(F, I);
                    setFunction(F, v);
                    
                    break;
                }
                case Instruction::BitCast:
                {
                    Value *v = I->getOperand(0);
                   
                    graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(v, VALUE));
                    vertices1.insert(std::make_pair(I, VALUE));
                    vertices1.insert(std::make_pair(v, VALUE));

                    setFunction(F, I);
                    setFunction(F, v);
                    
                    break;
                }
                case Instruction::Store:
                {
                    StoreInst *SI = dyn_cast<StoreInst>(I);
                    Value *v = SI->getValueOperand();
                    Value *ptr = SI->getPointerOperand();

                    for (INodeSet::const_iterator it = memoryGraphVi[std::make_pair(ptr, VALUE)].begin();
                            it != memoryGraphVi[std::make_pair(ptr, VALUE)].end(); ++it) {
                        int vv = (*it).first;
                        graphiV[std::make_pair(vv, VALUE)].insert(std::make_pair(v, VALUE));
                        vertices2.insert(std::make_pair(vv, VALUE));
                        vertices1.insert(std::make_pair(v, VALUE));
                    }
                    
                    for (VNodeSet::const_iterator it = memoryGraphVV[std::make_pair(ptr, VALUE)].begin();
                            it != memoryGraphVV[std::make_pair(ptr, VALUE)].end(); ++it) {
                        Value* vv = (*it).first;
                        graphVV[std::make_pair(vv, VALUE)].insert(std::make_pair(v, VALUE));
                        vertices1.insert(std::make_pair(vv, VALUE));
                        vertices1.insert(std::make_pair(v, VALUE));
                    }

                    // Verify if the instruction has constant expressions that
                    // read addresses of variables
                    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(v)) {
                        switch (CE->getOpcode()) {
                            case Instruction::GetElementPtr:
                            case Instruction::PtrToInt:
                            case Instruction::IntToPtr:
                                sources.insert(std::make_pair(CE, VALUE));

                                break;
                            case Instruction::BitCast:
                            {
                                BitCastInst *CI = dyn_cast<BitCastInst>(CE);

                                if (CI && CI->getDestTy()->isPointerTy())
                                    sources.insert(std::make_pair(CE, VALUE));
                                
                                break;
                            }
                        } 
                    }

                    setFunction(F, I);
                    setFunction(F, v);
                    setFunction(F, ptr);

                    break;
                }
                case Instruction::Load:
                {
                    LoadInst *LI = dyn_cast<LoadInst>(I);
                    Value *ptr = LI->getPointerOperand();

                    for (INodeSet::const_iterator it = memoryGraphVi[std::make_pair(ptr, VALUE)].begin();
                            it != memoryGraphVi[std::make_pair(ptr, VALUE)].end(); ++it) {
                        int vv = (*it).first;
                        graphVi[std::make_pair(I, VALUE)].insert(std::make_pair(vv, VALUE));
                        vertices1.insert(std::make_pair(I, VALUE));
                        vertices2.insert(std::make_pair(vv, VALUE));
                    }
                    
                    for (VNodeSet::const_iterator it = memoryGraphVV[std::make_pair(ptr, VALUE)].begin();
                            it != memoryGraphVV[std::make_pair(ptr, VALUE)].end(); ++it) {
                        Value* vv = (*it).first;
                        graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(vv, VALUE));
                        vertices1.insert(std::make_pair(I, VALUE));
                        vertices1.insert(std::make_pair(vv, VALUE));
                    }

                    setFunction(F, I);
                    setFunction(F, ptr);
                    
                    break;
                }
                case Instruction::Br: // Don't represent branches in the graph
                    break;
                case Instruction::PHI:
                { 
                    PHINode *Phi = dyn_cast<PHINode>(I);
                    unsigned n = Phi->getNumIncomingValues();
    
                    for (unsigned i = 0; i < n; i++) {
                        Value *v = Phi->getIncomingValue(i);

                        graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(v, VALUE));
                        vertices1.insert(std::make_pair(I, VALUE));
                        vertices1.insert(std::make_pair(v, VALUE));

                        setFunction(F, v);
                    }

                    setFunction(F, I);
                    break;
                }
                default:
                {
                    if (isa<CallInst>(I)) {
                        CallInst *CI = dyn_cast<CallInst>(I);

                        Function *FF = CI->getCalledFunction();

                        if (FF && !FF->getReturnType()->isVoidTy()) {
                            graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(CI->getCalledValue(), VALUE));
                            
                            vertices1.insert(std::make_pair(I, VALUE));
                            vertices1.insert(std::make_pair(CI->getCalledValue(), VALUE));
                        }

                        if (FF && FF->getName() == "itoa") {
                            Value *dst = CI->getOperand(1);

                            IntSet pointsTo = pointerAnalysis->pointsTo(Value2Int(dst));
                            
                            for (IntSet::const_iterator it = pointsTo.begin(), 
                                e = pointsTo.end(); it != e; it++) {
                                
                                if (int2value.count(*it)) {
                                    sources.insert(std::make_pair(int2value[*it], VALUE));
                                } else {
                                    sources2.insert(std::make_pair(*it, VALUE));
                                }
                            }
                        }
                    }

                    // Handle simple operations
                    for (unsigned i = 0; i < I->getNumOperands(); ++i) {
                        Value *v = I->getOperand(i);
                   
                        if (!isa<CallInst>(I)) {
                            graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(v, VALUE));
                            vertices1.insert(std::make_pair(I, VALUE));
                            vertices1.insert(std::make_pair(v, VALUE));
                        }

                        setFunction(F, v);
                      
                        // Verify if the instruction has constant expressions that
                        // read addresses of variables
                        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(v)) {
                            switch (CE->getOpcode()) {
                                case Instruction::GetElementPtr:
                                case Instruction::PtrToInt:
                                case Instruction::IntToPtr:
                                    sources.insert(std::make_pair(CE, VALUE));

                                    break;
                                case Instruction::BitCast:
                                {
                                    BitCastInst *CI = dyn_cast<BitCastInst>(CE);

                                    if (CI && CI->getDestTy()->isPointerTy())
                                        sources.insert(std::make_pair(CE, VALUE));
                                    
                                    break;
                                }
                            } 
                        }
                    }

                    setFunction(F, I);
                }
            }
        }
    }
}

char AddrLeaks::ID = 0;
static RegisterPass<AddrLeaks> X("addrleaks", "AddrLeaks Pass", false, false);

