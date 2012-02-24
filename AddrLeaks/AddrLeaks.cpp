/* Address Leak Analysis
 * 
 * This analysis is:
 *  - inter-procedural
 *  - context-sensitive
 *  - field-sensitive (up to one level)
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
        static char ID;
        AddrLeaks() : ModulePass(ID) { 
            nextMemoryBlock = 1;
        }
        
        bool runOnModule(Module &M);

      private:
        int nextMemoryBlock;
        enum nodeType {ADDR, VALUE};

        std::set<std::pair<Value*, nodeType> > sources;
        std::set<std::pair<int, nodeType> > sources2;
        std::set<Value*> sinks;
        
        std::set<std::pair<Value*, nodeType> > visited;
        std::set<std::pair<int, nodeType> > visited2;

        std::set<std::pair<Value*, nodeType> > visited3;
        std::set<std::pair<int, nodeType> > visited4;

        std::set<std::pair<Value*, nodeType> > vertices1;
        std::set<std::pair<int, nodeType> > vertices2;

        std::map<std::pair<Value*, nodeType>, std::set<std::pair<Value*, nodeType> > > memoryGraphVV;
        std::map<std::pair<Value*, nodeType>, std::set<std::pair<int, nodeType> > > memoryGraphVi;
        std::map<std::pair<int, nodeType>, std::set<std::pair<int, nodeType> > > memoryGraphii;
        std::map<std::pair<int, nodeType>, std::set<std::pair<Value*, nodeType> > > memoryGraphiV;

        std::map<std::pair<Value*, nodeType>, std::set<std::pair<Value*, nodeType> > > graphVV;
        std::map<std::pair<Value*, nodeType>, std::set<std::pair<int, nodeType> > > graphVi;
        std::map<std::pair<int, nodeType>, std::set<std::pair<Value*, nodeType> > > graphiV;

        std::map<Value*, std::vector<int> > memoryBlock;
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
    
    for (std::set<std::pair<Value*, nodeType> >::const_iterator it = graphiV[std::make_pair(v, t)].begin(); 
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

    if (sources.find(std::make_pair(v, t)) != sources.end())
        return true;
    
    for (std::set<std::pair<Value*, nodeType> >::const_iterator it = graphVV[std::make_pair(v, t)].begin(); 
            it != graphVV[std::make_pair(v, t)].end(); ++it) {
        Value *vv = (*it).first;
        nodeType tt = (*it).second;

        if (dfs(vv, tt)) {
            sources.insert(std::make_pair(v, t));
            return true;
        }
    }

    for (std::set<std::pair<int, nodeType> >::const_iterator it = graphVi[std::make_pair(v, t)].begin(); 
            it != graphVi[std::make_pair(v, t)].end(); ++it) {
        int vv = (*it).first;
        nodeType tt = (*it).second;

        if (dfs(vv, tt)) {
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
    
    for (std::set<std::pair<Value*, nodeType> >::const_iterator it = graphiV[std::make_pair(v, t)].begin(); 
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
    
    for (std::set<std::pair<Value*, nodeType> >::const_iterator it = graphVV[std::make_pair(v, t)].begin(); 
            it != graphVV[std::make_pair(v, t)].end(); ++it) {
        Value *vv = (*it).first;
        nodeType tt = (*it).second;

        total += countBuggyPathSize(vv, tt);
    }

    for (std::set<std::pair<int, nodeType> >::const_iterator it = graphVi[std::make_pair(v, t)].begin(); 
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

    for (std::map<std::pair<Value*, nodeType>, std::set<std::pair<int, nodeType> > >::const_iterator it = memoryGraphVi.begin();
            it != memoryGraphVi.end(); ++it) {
        Value *src = it->first.first;
        nodeType srcType = it->first.second;
        Function *f = function[src];
        std::string srcFunctionName = f ? f->getName().str() : "global";
 
        for (std::set<std::pair<int, nodeType> >::const_iterator it2 = it->second.begin(); it2 != it->second.end();
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

    for (std::map<std::pair<int, nodeType>, std::set<std::pair<int, nodeType> > >::const_iterator it = memoryGraphii.begin();
            it != memoryGraphii.end(); ++it) {
        int src = it->first.first;
        nodeType srcType = it->first.second;
        std::string srcFunctionName = "memory";
 
        for (std::set<std::pair<int, nodeType> >::const_iterator it2 = it->second.begin(); it2 != it->second.end();
                ++it2) {
            int dst = (*it2).first;
            nodeType dstType = (*it2).second;
            std::string dstFunctionName = "memory";
            
            out << "    \"(" << srcFunctionName << ") " << "m" << src << " " << nodeType2String(srcType) << "\" -> \"(" <<
                   dstFunctionName << ") " << "m" << dst << " " << nodeType2String(dstType) << "\" [style=dashed]\n"; 
        }
    }

    for (std::map<std::pair<int, nodeType>, std::set<std::pair<Value*, nodeType> > >::const_iterator it = memoryGraphiV.begin();
            it != memoryGraphiV.end(); ++it) {
        int src = it->first.first;
        nodeType srcType = it->first.second;
        std::string srcFunctionName = "memory";
 
        for (std::set<std::pair<Value*, nodeType> >::const_iterator it2 = it->second.begin(); it2 != it->second.end();
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

    for (std::map<std::pair<Value*, nodeType>, std::set<std::pair<Value*, nodeType> > >::const_iterator it = memoryGraphVV.begin();
            it != memoryGraphVV.end(); ++it) {
        Value *src = it->first.first;
        nodeType srcType = it->first.second;
        Function *f = function[src];
        std::string srcFunctionName = f ? f->getName().str() : "global";
 
        for (std::set<std::pair<Value*, nodeType> >::const_iterator it2 = it->second.begin(); it2 != it->second.end();
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

    for (std::map<std::pair<Value*, nodeType>, std::set<std::pair<Value*, nodeType> > >::const_iterator it = graphVV.begin();
            it != graphVV.end(); ++it) {
        Value *src = it->first.first;
        nodeType srcType = it->first.second;
        Function *f = function[src];
        std::string srcFunctionName = f ? f->getName().str() : "global";
 
        for (std::set<std::pair<Value*, nodeType> >::const_iterator it2 = it->second.begin(); it2 != it->second.end();
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

    for (std::map<std::pair<Value*, nodeType>, std::set<std::pair<int, nodeType> > >::const_iterator it = graphVi.begin();
            it != graphVi.end(); ++it) {
        Value *src = it->first.first;
        nodeType srcType = it->first.second;
        Function *f = function[src];
        std::string srcFunctionName = f ? f->getName().str() : "global";
 
        for (std::set<std::pair<int, nodeType> >::const_iterator it2 = it->second.begin(); it2 != it->second.end();
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

    for (std::map<std::pair<int, nodeType>, std::set<std::pair<Value*, nodeType> > >::const_iterator it = graphiV.begin();
            it != graphiV.end(); ++it) {
        int src = it->first.first;
        nodeType srcType = it->first.second;
        std::string srcFunctionName = "memory";
 
        for (std::set<std::pair<Value*, nodeType> >::const_iterator it2 = it->second.begin(); it2 != it->second.end();
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
    if (value2int.count(v))
        return value2int[v];
    
    int n = getNewInt();
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
            
                std::vector<bool> vaza;
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
                                formatString[i + 1] == 's' ||
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
                            // Just to collect some statistics. Will disappear in future
                            buggyPathsSize += countBuggyPathSize(v, VALUE);

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

    return false;
}

void AddrLeaks::setFunction(Function &F, Value *v) {
    // Set the associated function to NULL when its previous value is different of the current one
    if (function.count(v) && function[v] != &F)
        function[v] = 0;
    else
        function[v] = &F;
}

void AddrLeaks::addConstraints(Function &F) {
    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
        for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
            if (isa<CallInst>(I)) {
                CallInst *CI = dyn_cast<CallInst>(I);

                if (CI) {
                    Function *FF = CI->getCalledFunction();

                    if (FF && FF->getName() == "malloc") {
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
                    
                    break;
                }
                case Instruction::GetElementPtr:
                {
                    GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(I);
                    Value *v = GEPI->getPointerOperand();
                    const PointerType *PoTy = GEPI->getPointerOperandType();
                    const Type *Ty = PoTy->getElementType();

                    if (Ty->isStructTy() && memoryBlock.count(v)) {
                        int i = 0;
                        unsigned pos = 0;

                        for (User::op_iterator it = GEPI->idx_begin(), e = GEPI->idx_end(); it != e; ++it) {
                            if (i == 1)
                                pos = cast<ConstantInt>(*it)->getZExtValue();
                            
                            i++;
                        }
                        
                        std::vector<int> mems = memoryBlock[v];
                        int a = Value2Int(I);
                        pointerAnalysis->addBase(a, mems[pos]);
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
                    Value *v = I->getOperand(0);
                    
                    if (v->getType()->isPointerTy()) {
                        int a = Value2Int(I);
                        int b = Value2Int(v);
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
            }
        }
    }
}

void AddrLeaks::buildMemoryGraph() {
    std::map<int, IntSet> pointsTo = pointerAnalysis->allPointsTo();

    for (std::map<int, IntSet>::const_iterator it = pointsTo.begin(), e = pointsTo.end(); it != e; it++) {
        int src = it->first;

        for (IntSet::const_iterator it2 = it->second.begin(), 
            e = it->second.end(); it2 != e; it2++) {
            int dst = *it2;

            if (int2value.count(src) && int2value.count(dst))
                memoryGraphVV[std::make_pair(int2value[src], VALUE)].insert(std::make_pair(int2value[dst], ADDR));
            else if (!int2value.count(src) && int2value.count(dst))
                memoryGraphiV[std::make_pair(src,
                VALUE)].insert(std::make_pair(int2value[dst], ADDR));
            else if (int2value.count(src) && !int2value.count(dst))
                memoryGraphVi[std::make_pair(int2value[src], VALUE)].insert(std::make_pair(dst, ADDR));
            else if (!int2value.count(src) && !int2value.count(dst))
                memoryGraphii[std::make_pair(src, VALUE)].insert(std::make_pair(dst, ADDR));
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

                    if (FF && FF->getName() == "malloc") {
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

                    if (Ty->isStructTy() && memoryBlock.count(v)) {
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

                    for (std::set<std::pair<int, nodeType> >::const_iterator it = memoryGraphVi[std::make_pair(ptr, VALUE)].begin();
                            it != memoryGraphVi[std::make_pair(ptr, VALUE)].end(); ++it) {
                        int vv = (*it).first;
                        graphiV[std::make_pair(vv, VALUE)].insert(std::make_pair(v, VALUE));
                        vertices2.insert(std::make_pair(vv, VALUE));
                        vertices1.insert(std::make_pair(v, VALUE));
                    }
                    
                    for (std::set<std::pair<Value*, nodeType> >::const_iterator it = memoryGraphVV[std::make_pair(ptr, VALUE)].begin();
                            it != memoryGraphVV[std::make_pair(ptr, VALUE)].end(); ++it) {
                        Value* vv = (*it).first;
                        graphVV[std::make_pair(vv, VALUE)].insert(std::make_pair(v, VALUE));
                        vertices1.insert(std::make_pair(vv, VALUE));
                        vertices1.insert(std::make_pair(v, VALUE));
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

                    for (std::set<std::pair<int, nodeType> >::const_iterator it = memoryGraphVi[std::make_pair(ptr, VALUE)].begin();
                            it != memoryGraphVi[std::make_pair(ptr, VALUE)].end(); ++it) {
                        int vv = (*it).first;
                        graphVi[std::make_pair(I, VALUE)].insert(std::make_pair(vv, VALUE));
                        vertices1.insert(std::make_pair(I, VALUE));
                        vertices2.insert(std::make_pair(vv, VALUE));
                    }
                    
                    for (std::set<std::pair<Value*, nodeType> >::const_iterator it = memoryGraphVV[std::make_pair(ptr, VALUE)].begin();
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
                default:
                {
                    if (isa<CallInst>(I)) {
                        CallInst *CI = dyn_cast<CallInst>(I);
                        graphVV[std::make_pair(I, VALUE)].insert(std::make_pair(CI->getCalledValue(), VALUE));
                        vertices1.insert(std::make_pair(I, VALUE));
                        vertices1.insert(std::make_pair(CI->getCalledValue(), VALUE));
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

