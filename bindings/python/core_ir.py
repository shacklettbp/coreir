from ctypes import cdll
import ctypes as ct
import platform


def load_shared_lib():
    _system = platform.system()

    if _system == "Linux":
        shared_lib_ext = "so"
    elif _system == "Darwin":
        shared_lib_ext = "dylib"
    else:
        raise NotImplementedError(_system)

    return cdll.LoadLibrary('../../src/coreir.{}'.format(shared_lib_ext))

class EmptyStruct(ct.Structure):
    pass

# Pointers to typedefs use an empty struct as a placeholder
COREContext_p = ct.POINTER(EmptyStruct)
CORENamespace_p = ct.POINTER(EmptyStruct)
COREType_p = ct.POINTER(EmptyStruct)
COREModule_p = ct.POINTER(EmptyStruct)
COREModuleDef_p = ct.POINTER(EmptyStruct)
CORERecordParam_p = ct.POINTER(EmptyStruct)
COREInstance_p = ct.POINTER(EmptyStruct)
COREInterface_p = ct.POINTER(EmptyStruct)
CORESelect_p = ct.POINTER(EmptyStruct)
COREWireable_p = ct.POINTER(EmptyStruct)

coreir_lib = load_shared_lib()

coreir_lib.CORENewContext.restype = COREContext_p

coreir_lib.COREAny.argtypes = [COREContext_p]
coreir_lib.COREAny.restype = COREType_p

coreir_lib.COREBitIn.argtypes = [COREContext_p]
coreir_lib.COREBitIn.restype = COREType_p

coreir_lib.COREBitOut.argtypes = [COREContext_p]
coreir_lib.COREBitOut.restype = COREType_p

coreir_lib.COREArray.argtypes = [COREContext_p, ct.c_uint32, COREType_p]
coreir_lib.COREArray.restype = COREType_p

coreir_lib.CORENewRecordParam.argtypes = [COREContext_p]
coreir_lib.CORENewRecordParam.restype = CORERecordParam_p

coreir_lib.CORERecordParamAddField.argtypes = [COREContext_p, ct.c_char_p, COREType_p]

coreir_lib.CORERecord.argtypes = [COREContext_p, CORERecordParam_p]
coreir_lib.CORERecord.restype = COREType_p

coreir_lib.COREPrintType.argtypes = [COREType_p, ]

coreir_lib.CORELoadModule.argtypes = [COREContext_p, ct.c_char_p]
coreir_lib.CORELoadModule.restype = COREModule_p

coreir_lib.COREGetGlobal.argtypes = [COREContext_p]
coreir_lib.COREGetGlobal.restype = CORENamespace_p

coreir_lib.CORENewModule.argtypes = [CORENamespace_p, ct.c_char_p, COREType_p]
coreir_lib.CORENewModule.restype = COREModule_p

coreir_lib.COREModuleAddDef.argtypes = [COREModule_p, COREModuleDef_p]

coreir_lib.COREPrintModule.argtypes = [COREModule_p]

coreir_lib.COREModuleNewDef.argtypes = [COREModule_p]
coreir_lib.COREModuleNewDef.restype = COREModuleDef_p

coreir_lib.COREModuleDefAddInstanceModule.argtypes = [COREModuleDef_p, ct.c_char_p, COREModule_p]
coreir_lib.COREModuleDefAddInstanceModule.restype = COREInstance_p

coreir_lib.COREModuleDefGetInterface.argtypes = [COREModuleDef_p]
coreir_lib.COREModuleDefGetInterface.restype = COREInterface_p

coreir_lib.COREModuleDefWire.argtypes = [COREModuleDef_p, COREWireable_p, COREWireable_p]

coreir_lib.COREInterfaceSelect.argtypes = [COREInterface_p, ct.c_char_p]
coreir_lib.COREInterfaceSelect.restype = CORESelect_p

coreir_lib.COREInstanceSelect.argtypes = [COREInstance_p, ct.c_char_p]
coreir_lib.COREInstanceSelect.restype = CORESelect_p

coreir_lib.COREPrintModuleDef.argtypes = [COREModuleDef_p]


class CoreIRType:
    def __init__(self, ptr):
        self.ptr = ptr


class Type(CoreIRType):
    def print(self):
        coreir_lib.COREPrintType(self.ptr)


class Select(CoreIRType):
    pass

class Interface(CoreIRType):
    def select(self, field):
        return Select(coreir_lib.COREInterfaceSelect(self.ptr, str.encode(field)))

class Instance(CoreIRType):
    def select(self, field):
        return Select(coreir_lib.COREInterfaceSelect(self.ptr, str.encode(field)))


class ModuleDef(CoreIRType):
    def add_instance_module(self, name, module):
        assert isinstance(module,Module)
        return Instance(coreir_lib.COREModuleDefAddInstanceModule(self.ptr, str.encode(name), module.ptr))

    def get_interface(self):
        return Interface(coreir_lib.COREModuleDefGetInterface(self.ptr))

    def wire(self, a, b):
        coreir_lib.COREModuleDefWire(self.ptr, a.ptr, b.ptr)

    def print(self):
        coreir_lib.COREPrintModuleDef(self.ptr)


class Module(CoreIRType):
    def new_definition(self):
        return ModuleDef(coreir_lib.COREModuleNewDef(self.ptr))

    def add_definition(self, definition):
        assert isinstance(definition, ModuleDef)
        coreir_lib.COREModuleAddDef(self.ptr, definition.ptr)

    def print(self):
        coreir_lib.COREPrintModule(self.ptr)

class Namespace(CoreIRType):
  def Module(self, name, typ):
    return Module(
      coreir_lib.CORENewModule(self.ptr, ct.c_char_p(str.encode(name)), typ.ptr))

class Context:
    def __init__(self):
        self.context = coreir_lib.CORENewContext()
        self.G = Namespace(coreir_lib.COREGetGlobal(self.context))
    
    def GetG(self):
      return Namespace(coreir_lib.COREGetGlobal(self.context))
    
    def Any(self):
        return Type(coreir_lib.COREAny(self.context))

    def BitIn(self):
        return Type(coreir_lib.COREBitIn(self.context))

    def BitOut(self):
        return Type(coreir_lib.COREBitOut(self.context))

    def Array(self, length, typ):
        assert isinstance(typ, Type)
        assert isinstance(length, int)
        return Type(coreir_lib.COREArray(self.context, length, typ.ptr))

    def ModuleFromFile(self, file_name):
        return Module(
            coreir_lib.CORELoadModule(
                self.context, ct.c_char_p(str.encode(file_name))))

 
    def Record(self, fields):
        record_params = coreir_lib.CORENewRecordParam(self.context)
        for key, value in fields.items():
            coreir_lib.CORERecordParamAddField(record_params, str.encode(key), value.ptr)
        return Type(coreir_lib.CORERecord(self.context, record_params))

    def __del__(self):
        coreir_lib.COREDeleteContext(self.context)

if __name__ == "__main__":
    c = Context()
    # any = c.Any()
    # any.print()
    # c.BitIn().print()
    # c.BitOut().print()
    # c.Array(3, c.BitIn()).print()

    # c.Array(3, c.Array(4, c.BitIn())).print()

    # c.ModuleFromFile("test").print()
    module_typ = c.Record({"input": c.Array(8, c.BitIn()), "output": c.Array(9, c.BitOut())})
    module = c.G.Module("multiply_by_2", module_typ)
    module.print()
    module_def = module.new_definition()
    add8 = c.G.Module("add8",
        c.Record({
            "in1": c.Array(8, c.BitIn()),
            "in2": c.Array(8, c.BitIn()),
            "out": c.Array(9, c.BitOut())
        })
    )
    add8_inst = module_def.add_instance_module("adder", add8)
    add8_in1 = add8_inst.select("in1")
    add8_in2 = add8_inst.select("in2")
    add8_out = add8_inst.select("out")
    interface = module_def.get_interface()
    _input = interface.select("input")
    output = interface.select("output")
    module_def.wire(_input, add8_in1)
    module_def.wire(_input, add8_in2)
    module_def.wire(output, add8_out)
    module.add_definition(module_def)
    module.print()
