#include "coreir.h"
#include "cxxopts.hpp"
#include <dlfcn.h>
#include <fstream>

#include "coreir/passes/analysis/firrtl.h"
#include "coreir/passes/analysis/coreirjson.h"
#include "coreir/passes/analysis/verilog.h"

#include "../simulator/output.hpp"
#include "../simulator/sim.hpp"
#include "../simulator/utils.hpp"

using namespace std;
using namespace CoreIR;

string getExt(string s) {
  auto split = splitString<vector<string>>(s,'.');
  ASSERT(split.size()>=2,"File needs extention: " + s);
  return split[split.size()-1];
}

typedef std::map<std::string,std::pair<void*,Pass*>> OpenPassHandles_t;
typedef std::map<std::string,std::pair<void*,Namespace*>> OpenLibHandles_t;

int main(int argc, char *argv[]) {
  //int argc_copy = argc;
  cxxopts::Options options("coreir", "a simple hardware compiler");
  options.add_options()
    ("h,help","help")
    ("v,verbose","Set verbose")
    ("i,input","input file: <file>.json",cxxopts::value<std::string>())
    ("o,output","output file: <file>.<json|fir|v|dot>",cxxopts::value<std::string>())
    ("p,passes","Run passes in order: '<pass1>,<pass2>,<pass3>,...'",cxxopts::value<std::string>())
    ("e,load_passes","external passes: '<path1.so>,<path2.so>,<path3.so>,...'",cxxopts::value<std::string>())
    ("l,load_libs","external libs: '<path/libname0.so>,<path/libname1.so>,<path/libname2.so>,...'",cxxopts::value<std::string>())
    ("n,namespaces","namespaces to output: '<namespace1>,<namespace2>,<namespace3>,...'",cxxopts::value<std::string>()->default_value("global"))
    ;
  
  //Do the parsing of the arguments
  options.parse(argc,argv);
  
  OpenPassHandles_t openPassHandles;
  OpenLibHandles_t openLibHandles;
  
  
  Context* c = newContext();
  
  ASSERT(options.count("i"),"No input specified")
  string infileName = options["i"].as<string>();
  string inExt = getExt(infileName);
  ASSERT(inExt=="json","Input needs to be json");
  
  //Load input
  Module* top;
  if (!loadFromFile(c,infileName,&top)) {
    c->die();
  }
  string topRef = "";
  if (top) topRef = top->getRefName();

  NGraph gr;
  buildOrderedGraph(top, gr);
  deque<vdisc> topoOrder = topologicalSort(gr);

  string codePath = "./";
  string codeFile = top->getName() + "_sim.cpp";
  string hFile = top->getName() + "_sim.h";

  writeBitVectorLib(codePath + "bit_vector.h");
  writeFiles(topoOrder, gr, top, codePath, codeFile, hFile);
  
  return 0;
}
