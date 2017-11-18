#include "coreir/simulator/simulator.h"

#include "coreir/passes/transform/flatten.h"
#include "coreir/passes/transform/rungenerators.h"
#include "coreir/simulator/algorithm.h"
#include "coreir/simulator/print_c.h"
#include "coreir/simulator/utils.h"

using namespace CoreIR;
using namespace CoreIR::Passes;
using namespace std;

namespace CoreIR {

  string lastClkVarName(InstanceValue& clk) {
    return cVar("(state->", clk, "_last)");
  }

  string clkVarName(InstanceValue& clk) {
    return cVar("(state->", clk, ")");
  }
  
  string outputVarName(CoreIR::Wireable& outSel) {
    return cVar("(state->", outSel, ")");
  }

  string outputVarName(InstanceValue& val) {
    return cVar("(state->", val, ")");
  }
  
  string printBinop(const WireNode& wd, const vdisc vd, const NGraph& g);
  string printOpResultStr(const InstanceValue& wd, const NGraph& g);

  // wd is an instance node
  string opResultStr(const WireNode& wd, const vdisc vd, const NGraph& g);

  string printUnop(Instance* inst, const vdisc vd, const NGraph& g) {
    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, g);

    assert(inConns.size() == 1);

    Conn cn = (*std::begin(inConns));

    Wireable* dest = inConns[0].second.getWire();
    assert(isSelect(dest));

    Select* destSel = toSelect(dest);
    assert(destSel->getParent() == inst);

    string opString = getOpString(*inst);

    string val;

    if (opString != "andr") {
      val = opString + printOpResultStr(cn.first, g);
    } else {

      uint w = typeWidth(*(cn.first.getWire()->getType()));
      val = parens(printOpResultStr(cn.first, g) + " == " + bitMaskString(w));

    }

    string res =
      maskResult(*((outPair.second)->getType()),
                 val);

    return res;
  }

  string printBVConstant(Instance* inst, const vdisc vd, const NGraph& g) {

    bool foundValue = false;

    string argStr = "";
    for (auto& arg : inst->getModArgs()) {
      if (arg.first == "value") {
        foundValue = true;
        Value* valArg = arg.second;

        cout << "Value type = " << valArg->getValueType()->toString() << endl;

        BitVector bv = valArg->get<BitVector>();
        stringstream ss;
        ss << "0b" << bv;
        argStr = ss.str();
      }
    }

    assert(foundValue);

    return argStr;
  }

  string printBitConstant(Instance* inst, const vdisc vd, const NGraph& g) {

    bool foundValue = false;

    string argStr = "";
    for (auto& arg : inst->getModArgs()) {
      if (arg.first == "value") {
        foundValue = true;
        Value* valArg = arg.second; //.get();

        cout << "Value type = " << valArg->getValueType()->toString() << endl;

        assert(valArg->getValueType() == inst->getContext()->Bool());
        //assert(valArg->getKind() == AINT);

        //ArgInt* valInt = static_cast<ArgInt*>(valArg);
        bool bv = valArg->get<bool>();
        stringstream ss;
        ss << (bv ? "1" : "0");
        argStr = ss.str(); //std::to_string(valArg->get<int>()); //valInt->toString();
      }
    }

    assert(foundValue);

    return argStr;
  }

  string printConstant(Instance* inst, const vdisc vd, const NGraph& g) {
    if (getQualifiedOpName(*inst) == "corebit.const") {
      return printBitConstant(inst, vd, g);
    } else {
      return printBVConstant(inst, vd, g);
    }
  }

  string printOpThenMaskBinop(const WireNode& wd, const vdisc vd, const NGraph& g) {
    Instance* inst = toInstance(wd.getWire());

    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    string res = "";

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, g);

    assert(inConns.size() == 2);

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);

    string opString = getOpString(*inst);

    string compString =
      parens(printOpResultStr(arg1, g) + opString + printOpResultStr(arg2, g));

    // And not standard width
    if (isDASHR(*inst)) {
      uint tw = typeWidth(*(arg1.getWire()->getType()));
      uint containWidth = containerTypeWidth(*(arg1.getWire()->getType()));
      if (containWidth > tw) {

        string mask =
          parens(bitMaskString(printOpResultStr(arg2, g)) + " << " + parens(to_string(tw) + " - " + printOpResultStr(arg2, g)));

        string signBitSet =
          parens("0x01 & " + parens(printOpResultStr(arg1, g) +  " >> " + parens(to_string(tw - 1))));

        compString = parens(ite(signBitSet, mask, "0") + " | " + parens(compString));
      }
    }

    // Check if this output needs a mask
    if (g.getOutputConnections(vd)[0].first.needsMask()) {
      res += maskResult(*(outPair.second->getType()), compString);
    } else {
      res += compString;
    }

    return res;
  }

  string castToSigned(Type& tp, const std::string& expr) {
    return parens(parens(signedCTypeString(tp)) + " " + expr);
  }

  string castToUnSigned(Type& tp, const std::string& expr) {
    return parens(parens(unSignedCTypeString(tp)) + " " + expr);
  }

  string seString(Type& tp, const std::string& arg) {


    uint startWidth = typeWidth(tp);
    uint extWidth = containerTypeWidth(tp);

    if (startWidth < extWidth) {
      return "SIGN_EXTEND( " + to_string(startWidth) + ", " +
        to_string(extWidth) + ", " +
        arg + " )";
    } else if (startWidth == extWidth) {
      return parens(arg);
    } else {
      cout << "ERROR: trying to sign extend from " << startWidth << " to " << extWidth << endl;
      assert(false);
    }

  }

  string
  printSEThenOpThenMaskBinop(Instance* inst, const vdisc vd, const NGraph& g) {
    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, g);

    assert(inConns.size() == 2);

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);

    string opString = getOpString(*inst);

    Type& arg1Tp = *((arg1.getWire())->getType());
    Type& arg2Tp = *((arg2.getWire())->getType());

    string rs1 = printOpResultStr(arg1, g);
    string rs2 = printOpResultStr(arg2, g);

    string opStr = castToSigned(arg1Tp, seString(arg1Tp, rs1)) +
      opString +
      castToSigned(arg2Tp, seString(arg2Tp, rs2));

    string res;
    if (g.getOutputConnections(vd)[0].first.needsMask()) {
      res += maskResult(*(outPair.second->getType()), opStr);
    } else {
      res += opStr;
    }
      
    return res;
  }

  bool isMux(Instance& inst) {

    string genRefName = getInstanceName(inst);

    return genRefName == "mux";

  }

  string printMux(Instance* inst, const vdisc vd, const NGraph& g) {
    assert(isMux(*inst));

    auto ins = getInputConnections(vd, g);

    assert(ins.size() == 3);

    InstanceValue sel = findArg("sel", ins);
    InstanceValue i0 = findArg("in0", ins);
    InstanceValue i1 = findArg("in1", ins);
    
    return ite(printOpResultStr(sel, g),
               printOpResultStr(i1, g),
               printOpResultStr(i0, g));
  }

  string printAddOrSubWithCIN(const WireNode& wd, const vdisc vd, const NGraph& g) {
    auto ins = getInputs(vd, g);

    assert(ins.size() == 3);
    
    Instance* inst = toInstance(wd.getWire());
    auto outSelects = getOutputSelects(inst);

    assert((outSelects.size() == 1));

    string res = "";

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, g);

    // Either it is a binop or there is a cin
    assert((inConns.size() == 2) || (inConns.size() == 3));

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);
    InstanceValue carry = findArg("cin", inConns);

    string opString = getOpString(*inst);

    string compString =
      parens(printOpResultStr(arg1, g) + opString + printOpResultStr(arg2, g) + " + " + printOpResultStr(carry, g));

    // Check if this output needs a mask
    if (g.getOutputConnections(vd)[0].first.needsMask()) {
      res += maskResult(*(outPair.second->getType()), compString);
    } else {
      res += compString;
    }

    return res;

  }

  string checkSumOverflowStr(Type& tp,
                             const std::string& in0StrNC,
                             const std::string& in1StrNC) {
    string in0Str = castToUnSigned(tp, in0StrNC);
    string in1Str = castToUnSigned(tp, in0StrNC);

    string sumString = castToUnSigned(tp, parens(in0StrNC + " + " + in1StrNC));
    string test1 = parens(sumString + " < " + in0Str);
    string test2 = parens(sumString + " < " + in1Str);
    return parens(test1 + " || " + test2);
  }

  // NOTE: This function prints the full assignment of values
  string printAddOrSubCIN_COUT(const WireNode& wd, const vdisc vd, const NGraph& g) {
    auto ins = getInputs(vd, g);

    assert(ins.size() == 3);
    
    Instance* inst = toInstance(wd.getWire());
    auto outSelects = getOutputSelects(inst);

    assert((outSelects.size() == 2));

    Wireable* resultSelect = findSelect("out", outSelects);
    Wireable* coutSelect = findSelect("cout", outSelects);

    string res = "";

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, g);

    // Either it is a binop or there is a cin
    assert((inConns.size() == 2) || (inConns.size() == 3));

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);
    InstanceValue carry = findArg("cin", inConns);

    string opString = getOpString(*inst);

    string in0Str = printOpResultStr(arg1, g);
    string in1Str = printOpResultStr(arg2, g);
    string carryStr = printOpResultStr(carry, g);
    string sumStr = parens(in0Str + opString + in1Str);

    string compString =
      parens(sumStr + " + " + carryStr);

    Type& tp = *(resultSelect->getType());
    res += maskResult(tp, compString);

    // This does not actually handle the case where the underlying types are the
    // a fixed architecture width
    string carryRes;
    if (standardWidth(tp)) {
      string firstOverflow = checkSumOverflowStr(tp, in0Str, in1Str);
      string secondOverflow = checkSumOverflowStr(tp, sumStr, carryStr);
      carryRes = parens(firstOverflow + " || " + secondOverflow);
    } else {

      carryRes = parens(parens(compString + " >> " + to_string(typeWidth(tp))) + " & 0x1");

    }

    string carryString = cVar(*coutSelect) + " = " + carryRes;

    return ln(cVar(*resultSelect) + " = " + res) + ln(carryString);

  }

  // NOTE: This function prints the full assignment of values
  string printAddOrSubCOUT(const WireNode& wd, const vdisc vd, const NGraph& g) {
    auto ins = getInputs(vd, g);

    assert(ins.size() == 2);
    
    Instance* inst = toInstance(wd.getWire());
    auto outSelects = getOutputSelects(inst);

    assert((outSelects.size() == 2));

    Wireable* resultSelect = findSelect("out", outSelects);
    Wireable* coutSelect = findSelect("cout", outSelects);

    string res = "";

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, g);

    // Either it is a binop or there is a cin
    assert((inConns.size() == 2) || (inConns.size() == 3));

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);

    string opString = getOpString(*inst);

    string in0Str = printOpResultStr(arg1, g);
    string in1Str = printOpResultStr(arg2, g);
    string sumStr = parens(in0Str + opString + in1Str);

    string compString = sumStr;

    Type& tp = *(resultSelect->getType());
    res += maskResult(tp, compString);

    // This does not actually handle the case where the underlying types are the
    // a fixed architecture width
    string carryRes;
    if (standardWidth(tp)) {
      string firstOverflow = checkSumOverflowStr(tp, in0Str, in1Str);
      carryRes = parens(firstOverflow);
    } else {

      carryRes = parens(parens(compString + " >> " + to_string(typeWidth(tp))) + " & 0x1");

    }

    string carryString = cVar(*coutSelect) + " = " + carryRes;

    return ln(cVar(*resultSelect) + " = " + res) + ln(carryString);

  }
  
  string printTernop(const WireNode& wd, const vdisc vd, const NGraph& g) {
    assert(getInputs(vd, g).size() == 3);

    Instance* inst = toInstance(wd.getWire());
    if (isMux(*inst)) {
      return printMux(inst, vd, g);
    }

    if (isAddOrSub(*inst)) {
      // Add and subtract need special treatment because of cin and cout flags
      return printAddOrSubWithCIN(wd, vd, g);
    }

    assert(false);
  }

  string printBinop(const WireNode& wd, const vdisc vd, const NGraph& g) {
    assert(getInputs(vd, g).size() == 2);

    Instance* inst = toInstance(wd.getWire());

    if (isBitwiseOp(*inst) ||
        isSignInvariantOp(*inst) ||
        isUnsignedCmp(*inst) ||
        isShiftOp(*inst) ||
        isUDivOrRem(*inst)) {
      return printOpThenMaskBinop(wd, vd, g);
    }

    if (isSignedCmp(*inst) ||
        isSDivOrRem(*inst)) {
      return printSEThenOpThenMaskBinop(inst, vd, g);
    }

    cout << "Unsupported binop = " << inst->toString() << " from module = " << inst->getModuleRef()->getName() << endl;

    assert(false);
  }

  bool hasEnable(Wireable* w) {
    assert(isRegisterInstance(w));

    return recordTypeHasField("en", w->getType());
  }

  string enableRegReceiver(const WireNode& wd, const vdisc vd, const NGraph& g) {

    auto outSel = getOutputSelects(wd.getWire());

    assert(outSel.size() == 1);
    Select* sl = toSelect((*(begin(outSel))).second);

    assert(isInstance(sl->getParent()));

    Instance* r = toInstance(sl->getParent());
    string rName = r->getInstname();

    auto ins = getInputConnections(vd, g);

    assert((ins.size() == 3) || (ins.size() == 2 && !hasEnable(wd.getWire())));

    //string s = cVar("(state->", *toInstance(wd.getWire()), ")") + " = ";
    string s = outputVarName(*wd.getWire()) + " = ";

    InstanceValue clk = findArg("clk", ins);
    InstanceValue add = findArg("in", ins);

    //string oldValName = cVar("(state->", *r, ")");
    string oldValName = outputVarName(*r); //cVar("(state->", *r, ")");

    // Need to handle the case where clock is not actually directly from an input
    // clock variable
    string condition =
      //parens(cVar("(state->", clk, "_last)") + " == 0") + " && " + parens(cVar("(state->", clk, ")") + " == 1");
      parens(parens(lastClkVarName(clk) + " == 0") + " && " +
             parens(clkVarName(clk) + " == 1"));

    if (hasEnable(wd.getWire())) {
      InstanceValue en = findArg("en", ins);
      condition += " && " + printOpResultStr(en, g);
    }

    s += ite(parens(condition),
             printOpResultStr(add, g),
             oldValName) + ";\n";
    
    return s;
  }

  string printRegister(const WireNode& wd, const vdisc vd, const NGraph& g) {
    assert(wd.isSequential);

    auto outSel = getOutputSelects(wd.getWire());

    assert(outSel.size() == 1);
    Select* s = toSelect((*(begin(outSel))).second);

    assert(isInstance(s->getParent()));

    Instance* r = toInstance(s->getParent());
    string rName = r->getInstname();

    
    if (!wd.isReceiver) {
      //return "";
      return ln(cVar(*s) + " = " + outputVarName(*r)); //cVar("(state->", *r, ")"));
    } else {
      return enableRegReceiver(wd, vd, g);
    }
  }

  string opResultStr(const WireNode& wd, const vdisc vd, const NGraph& g) {

    Instance* inst = toInstance(wd.getWire());
    auto ins = getInputs(vd, g);
    
    if (ins.size() == 3) {
      return printTernop(wd, vd, g);
    }

    if (ins.size() == 2) {
      return printBinop(wd, vd, g);
    }

    if (ins.size() == 1) {
      return printUnop(inst, vd, g);
    }

    if (ins.size() == 0) {

      return printConstant(inst, vd, g);
    }

    cout << "Unsupported instance = " << inst->toString() << endl;
    assert(false);
    return "";
  }

  string printMemory(const WireNode& wd, const vdisc vd, const NGraph& g) {
    assert(wd.isSequential);

    auto outSel = getOutputSelects(wd.getWire());
    
    assert(outSel.size() == 1);
    Select* s = toSelect((*(begin(outSel))).second);
    
    assert(isInstance(s->getParent()));

    Instance* r = toInstance(s->getParent());

    auto ins = getInputConnections(vd, g);
    
    if (!wd.isReceiver) {
      assert(ins.size() == 1);

      InstanceValue raddr = findArg("raddr", ins);
      // return ln(cVar(*s) + " = " +
      //           parens(cVar("(state->", *r, ")") +
      //                  "[ " + printOpResultStr(raddr, g) + " ]"));

      return ln(cVar(*s) + " = " +
                parens(outputVarName(*r) +
                       "[ " + printOpResultStr(raddr, g) + " ]"));

    } else {
      assert(ins.size() == 4);

      InstanceValue waddr = findArg("waddr", ins);
      InstanceValue wdata = findArg("wdata", ins);
      InstanceValue clk = findArg("clk", ins);
      InstanceValue wen = findArg("wen", ins);

      string condition =
        parens(parens(lastClkVarName(clk) + " == 0") + " && " +
               parens(clkVarName(clk) + " == 1"));
        
        // parens(cVar("(state->", clk, "_last)") + " == 0") + " && " + parens(cVar("(state->", clk, ")") + " == 1");

      condition += " && " + printOpResultStr(wen, g);

      //string oldValueName = cVar("(state->", *r, ")") + "[ " + printOpResultStr(waddr, g) + " ]";
      string oldValueName = outputVarName(*r) + "[ " + printOpResultStr(waddr, g) + " ]";

      string s = oldValueName + " = ";
      s += ite(parens(condition),
               printOpResultStr(wdata, g),
               oldValueName);

      return ln(s);
      
    }
  }

  string printInstance(const WireNode& wd, const vdisc vd, const NGraph& g) {
    Instance* inst = toInstance(wd.getWire());

    if (isRegisterInstance(inst)) {
      return printRegister(wd, vd, g);
    }

    if (isMemoryInstance(inst)) {
      return printMemory(wd, vd, g);
    }

    auto outSelects = getOutputSelects(inst);

    if (outSelects.size() == 1) {

    pair<string, Wireable*> outPair = *std::begin(outSelects);
    string res;
    if (!isThreadShared(vd, g)) {
      res = cVar(*(outPair.second));
    } else {
      //res = cVar("(state->", *(outPair.second), ")");
      res = outputVarName(*(outPair.second));
    }

    
      return ln(res + " = " + opResultStr(wd, vd, g));
    } else {
      assert(outSelects.size() == 2);
      assert(isAddOrSub(*inst));

      auto ins = getInputs(vd, g);

      if (ins.size() == 3) {
      
        return printAddOrSubCIN_COUT(wd, vd, g);
      } else {
        assert(ins.size() == 2);

        return printAddOrSubCOUT(wd, vd, g);
        
      }
    }
  }

  bool isCombinationalInstance(const WireNode& wd) {
    assert(isInstance(wd.getWire()));

    if (isRegisterInstance(wd.getWire())) {
      return false;
    }
    if (isMemoryInstance(wd.getWire())) {
      cout << "Found memory instance" << endl;
      return false;
    }

    return true;
  }

  string printOpResultStr(const InstanceValue& wd, const NGraph& g) {
    assert(isSelect(wd.getWire()));

    Wireable* src = extractSource(toSelect(wd.getWire()));

    if (isRegisterInstance(src)) {
      //return cVar("(state->", *src, ")");
      return outputVarName(*src);
    }

    if (isMemoryInstance(src)) {
      return cVar(wd);
    }

    Wireable* sourceInstance = extractSource(toSelect(wd.getWire()));

    // Is this the correct way to check if the value is an input?
    if (isSelect(sourceInstance) && fromSelf(toSelect(sourceInstance))) {
      //return cVar("(state->", wd, ")");
      return outputVarName(wd);
    }

    if (isThreadShared(g.getOpNodeDisc(sourceInstance), g)) {
      //return cVar("(state->", wd, ")");
      return outputVarName(wd);
    }
    assert(g.containsOpNode(sourceInstance));

    vdisc opNodeD = g.getOpNodeDisc(sourceInstance);

    // TODO: Should really check whether or not there is one connection using
    // the given variable, this is slightly too conservative
    if (g.getOutputConnections(opNodeD).size() == 1) {
      return opResultStr(combNode(sourceInstance), opNodeD, g);
    }

    return cVar(wd);
  }

  string printInternalVariables(const std::deque<vdisc>& topo_order,
                                NGraph& g,
                                Module&) {
    string str = "";
    for (auto& vd : topo_order) {
      WireNode wd = getNode( g, vd);
      Wireable* w = wd.getWire();

      for (auto inSel : getOutputSelects(w)) {
        Select* in = toSelect(inSel.second);

        if (!fromSelfInterface(in)) {
          if (!arrayAccess(in)) {

            if (!wd.isSequential) {

              str += cArrayTypeDecl(*(in->getType()), " " + cVar(*in)) + ";\n";


            } else {
              if (wd.isReceiver) {
                str += cArrayTypeDecl(*(in->getType()), " " + cVar(*in)) + ";\n";
              }
            }
          }
        }
      }
    }

    return str;
  }

  string printSimFunctionPrefix(const std::deque<vdisc>& topo_order,
                              NGraph& g,
                              Module& mod,
                              const int threadNo) {
    string str = "";

    // Declare all variables
    str += "\n// Variable declarations\n";

    str += "\n// Internal variables\n";
    str += printInternalVariables(topo_order, g, mod);

    return str;
  }

  string printSimFunctionBody(const std::deque<vdisc>& topo_order,
                              NGraph& g,
                              Module& mod,
                              const int threadNo) {
    cout << "Printing sim function for " << threadNo << endl;

    string str = printSimFunctionPrefix(topo_order, g, mod, threadNo);

    // Print out operations in topological order
    str += "\n// Simulation code\n";

    int i = 0;
    vector<string> simLines;

    // Update stateful element values
    simLines.push_back("// Update stored state in sequential elements\n");
    for (auto& vd : topo_order) {

      WireNode wd = getNode(g, vd);

      if (wd.getThreadNo() == threadNo) {

        Wireable* inst = wd.getWire();

        if (isInstance(inst)) { 

          if (!isCombinationalInstance(wd) &&
              wd.isReceiver) {

            simLines.push_back(printInstance(wd, vd, g));

          }

        }
      }
      
    }

    simLines.push_back("// Update outputs of sequential elements\n");
    for (auto& vd : topo_order) {

      WireNode wd = getNode(g, vd);

      if (wd.getThreadNo() == threadNo) {

        Wireable* inst = wd.getWire();

        if (isInstance(inst)) { 

          if (!isCombinationalInstance(wd) &&
              !(wd.isReceiver)) {

            simLines.push_back(printInstance(wd, vd, g));

          }

        }
      }
      
    }
    
    simLines.push_back("// Update combinational logic\n");
    for (auto& vd : topo_order) {

      string val = "<UNSET>";
      WireNode wd = getNode(g, vd);

      if (wd.getThreadNo() == threadNo) {

        Wireable* inst = wd.getWire();

        if (isInstance(inst)) { 

          if ((isCombinationalInstance(wd)) &&
              ((g.getOutputConnections(vd).size() > 1) ||
               (isThreadShared(vd, g) && wd.getThreadNo() == threadNo))) {

            simLines.push_back(printInstance(wd, vd, g));

          }

        } else {

          if (inst->getType()->isInput()) {

            auto inConns = getInputConnections(vd, g);

            // If not an instance copy the input values
            for (auto inConn : inConns) {

              Wireable& outSel = *(inConn.second.getWire());
              string outVarName = outputVarName(outSel);

              simLines.push_back(ln(outVarName + " = " + printOpResultStr(inConn.first, g)));
              
            }

          }
        }
      }

      if ((i % 500) == 0) {
        cout << "Code for instance " << i << " = " << val << endl;
      }
      i++;
    }

    cout << "Done writing sim lines, now need to concatenate them" << endl;

    for (auto& ln : simLines) {
      str += ln;
    }

    cout << "Done concatenating" << endl;

    return str; //ss.str(); //str;
  }

  bool underlyingTypeIsClkIn(Type& tp) {
    if (isClkIn(tp)) {
      return true;
    }

    if (isArray(tp)) {
      ArrayType& tarr = toArray(tp);
      return underlyingTypeIsClkIn(*(tarr.getElemType()));
    }

    return false;

  }

  std::vector<std::pair<CoreIR::Type*, std::string> >
  simMemoryInputs(Module& mod) {
    vector<pair<Type*, string>> declStrs;
    
    // Add register inputs
    for (auto& inst : mod.getDef()->getInstances()) {
      if (isMemoryInstance(inst.second)) {
        cout << "Adding memory instance" << endl;
        Instance* is = inst.second;

        Context* c = mod.getDef()->getContext();

        Values args = is->getModuleRef()->getGenArgs();

        auto wArg = args["width"];
        auto dArg = args["depth"];
        
        uint width = wArg->get<int>(); //16;
        uint depth = dArg->get<int>();
        Type* elemType = c->Array(depth, c->Array(width, c->BitIn()));
        declStrs.push_back({elemType, is->toString()});

      }
    }

    return declStrs;
  }  

  std::vector<std::pair<CoreIR::Type*, std::string> >
  simRegisterInputs(Module& mod) {

    vector<pair<Type*, string>> declStrs;
    
    // Add register inputs
    for (auto& inst : mod.getDef()->getInstances()) {
      if (isRegisterInstance(inst.second)) {
        Instance* is = inst.second;

        Select* in = is->sel("in");
        Type* itp = in->getType();

        string regName = is->getInstname();

        declStrs.push_back({itp, cVar(*is)});
        
      }
    }

    return declStrs;
    
  }

  std::vector<std::pair<CoreIR::Type*, std::string> >
  threadSharedVariableDecls(const NGraph& g) {
    vector<pair<Type*, string>> declStrs;

    for (auto& vd : g.getVerts()) {
      WireNode wd = getNode( g, vd);
      Wireable* w = wd.getWire();

      if (isThreadShared(vd, g)) {
        for (auto inSel : getOutputSelects(w)) {
          Select* in = toSelect(inSel.second);

          if (!fromSelfInterface(in)) {
            if (!arrayAccess(in)) {

              if (!wd.isSequential) {

                declStrs.push_back({in->getType(), cVar(*in)});
                //str += cArrayTypeDecl(*(in->getType()), " " + cVar(*in)) + ";\n";

              }
            }
          }
        }
      }
    }

    return declStrs;
  }

  std::vector<std::pair<CoreIR::Type*, std::string> >
  sortedSimArgumentPairs(Module& mod) {

    Type* tp = mod.getType();

    assert(tp->getKind() == Type::TK_Record);

    RecordType* modRec = static_cast<RecordType*>(tp);
    vector<pair<Type*, string>> declStrs;

    for (auto& name_type_pair : modRec->getRecord()) {
      Type* tp = name_type_pair.second;

      if (tp->isInput()) {
        if (!underlyingTypeIsClkIn(*tp)) {
          declStrs.push_back({tp, "self_" + name_type_pair.first});
        } else {
          declStrs.push_back({tp, "self_" + name_type_pair.first});
          declStrs.push_back({tp, "self_" + name_type_pair.first + "_last"});

        }
      } else {
        assert(tp->isOutput());

        declStrs.push_back({tp, "self_" + name_type_pair.first});
        
        //declStrs.push_back({tp, "(*self_" + name_type_pair.first + "_ptr)"});
      }
    }

    // Add register inputs
    concat(declStrs, simRegisterInputs(mod));
    // Add memory inputs
    concat(declStrs, simMemoryInputs(mod));
    

    return declStrs;
    
  }

  std::vector<string> sortedSimArgumentList(Module& mod,
                                            const NGraph& g) {

    auto decls = sortedSimArgumentPairs(mod);

    concat(decls, threadSharedVariableDecls(g));
    
    sort_lt(decls, [](const pair<Type*, string>& tpp) {
        return tpp.second;
      });

    vector<string> declStrs;
    for (auto declPair :  decls) {
      declStrs.push_back(cArrayTypeDecl(*(declPair.first), declPair.second));
    }

    return declStrs;
  }

  std::string printEvalStruct(CoreIR::Module* mod,
                              const NGraph& g) {
    string res = "struct circuit_state {\n";

    auto declStrs = sortedSimArgumentList(*mod, g);
    for (auto& dstr : declStrs) {
      res += "\t" + dstr + ";\n";
    }
    
    res += "};\n\n";

    return res;
  }  

  // Note: Dont actually need baseName here
  string printDecl(CoreIR::Module* mod,
                   const NGraph& g) {
    string code = "";
    code += "#include <stdint.h>\n";
    code += "#include <cstdio>\n\n";
    code += "#include \"bit_vector.h\"\n\n";

    code += "using namespace bsim;\n\n";

    code += printEvalStruct(mod, g);
    code += "void simulate( circuit_state* state );\n";

    return code;
  }

  string printCode(const std::deque<vdisc>& topoOrder,
                   NGraph& g,
                   CoreIR::Module* mod,
                   const std::string& baseName) {

    string code = "";

    code += "#include \"" + baseName + "\"\n";
    code += "#include <thread>\n\n";

    code += "using namespace bsim;\n\n";

    code += seMacroDef();
    code += maskMacroDef();

    ThreadGraph tg = buildThreadGraph(g);

    for (auto& i : tg.getVerts()) {
      code += "void simulate_" + to_string(i) + "( circuit_state* state ) {\n";

      code += printSimFunctionBody(topoOrder, g, *mod, i);

      code += "}\n\n";

    }

    deque<vdisc> unPrintedThreads = topologicalSort(tg);
    vector<vdisc> unJoinedThreads;
    for (auto& vd : unPrintedThreads) {
      unJoinedThreads.push_back(vd);
    }

    code += "void simulate( circuit_state* state ) {\n";

    if (unPrintedThreads.size() == 1) {
      string iStr = to_string(unPrintedThreads[0]);

      code += ln("simulate_" + iStr + "( state )");

    } else {

      for (auto i : unPrintedThreads) {
        string iStr = to_string(i);

        // Join threads that this thread depends on
        for (auto depEdge : tg.inEdges(i)) {
          vdisc se = tg.source(depEdge);
          if (elem(se, unJoinedThreads)) {
            code += ln("simulate_" + to_string(se) + "_thread.join()");
            remove(se, unJoinedThreads);
            cout << "Joined thread " << se << endl;

          }
          
        }
        code += ln("std::thread simulate_" + iStr + "_thread( simulate_" + iStr + ", state )");
      }

      // Join all remaining threads before simulate function ends
      for (auto i : unJoinedThreads) {
        string iStr = to_string(i);
        code += ln("simulate_" + iStr + "_thread.join()");
      }
    }

    code += "}\n";

    return code;
  }

}
