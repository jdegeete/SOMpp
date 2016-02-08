#include <vmobjects/VMSymbol.h>
#include <vmobjects/VMObject.h>
#include <vmobjects/VMMethod.h>
#include <vmobjects/VMClass.h>
#include <vmobjects/VMFrame.h>
#include <vmobjects/VMArray.h>
#include <vmobjects/VMBlock.h>
#include <vmobjects/VMDouble.h>
#include <vmobjects/VMInteger.h>
#include <vmobjects/VMString.h>
#include <vmobjects/VMEvaluationPrimitive.h>

#ifdef GENERATE_ALLOCATION_STATISTICS
struct alloc_data {long noObjects; long sizeObjects;};
std::map<std::string, struct alloc_data> allocationStats;
#define LOG_ALLOCATION(TYPE,SIZE) {struct alloc_data tmp=allocationStats[TYPE];tmp.noObjects++;tmp.sizeObjects+=(SIZE);allocationStats[TYPE]=tmp;}
#else
#define LOG_ALLOCATION(TYPE,SIZE)
#endif

UniverseFactory::UniverseFactory() {
    this->universe = nullptr;
}

UniverseFactory::UniverseFactory(Universe* universe) {
    this->universe = universe;
}

VMArray* UniverseFactory::NewArray(long size) const {
    long additionalBytes = size * sizeof(VMObject*);
    
    bool outsideNursery;
    
#if GC_TYPE == GENERATIONAL
    // if the array is too big for the nursery, we will directly allocate a
    // mature object
    outsideNursery = additionalBytes + sizeof(VMArray) > GetHeap<HEAP_CLS>()->GetMaxNurseryObjectSize();
#endif
    
    VMArray* result = new (GetHeap<HEAP_CLS>(), additionalBytes ALLOC_OUTSIDE_NURSERY(outsideNursery)) VMArray(size);
    if ((GC_TYPE == GENERATIONAL) && outsideNursery)
        result->SetGCField(MASK_OBJECT_IS_OLD);
    
    result->SetClass(load_ptr(arrayClass));
    
    LOG_ALLOCATION("VMArray", result->GetObjectSize());
    return result;
}

VMArray* UniverseFactory::NewArrayFromStrings(const vector<StdString>& argv) const {
    VMArray* result = NewArray(argv.size());
    long j = 0;
    for (vector<StdString>::const_iterator i = argv.begin();
         i != argv.end(); ++i) {
        result->SetIndexableField(j, NewString(*i));
        ++j;
    }
    
    return result;
}

VMArray* UniverseFactory::NewArrayList(ExtendedList<VMSymbol*>& list) const {
    ExtendedList<vm_oop_t>& objList = (ExtendedList<vm_oop_t>&) list;
    return NewArrayList(objList);
}

VMArray* UniverseFactory::NewArrayList(ExtendedList<VMInvokable*>& list) const {
    ExtendedList<vm_oop_t>& objList = (ExtendedList<vm_oop_t>&) list;
    return NewArrayList(objList);
}

VMArray* UniverseFactory::NewArrayList(ExtendedList<vm_oop_t>& list) const {
    long size = list.Size();
    VMArray* result = NewArray(size);
    
    if (result) {
        for (long i = 0; i < size; ++i) {
            vm_oop_t elem = list.Get(i);
            result->SetIndexableField(i, elem);
        }
    }
    return result;
}

VMBlock* UniverseFactory::NewBlock(VMMethod* method, VMFrame* context, long arguments) {
    VMBlock* result = new (GetHeap<HEAP_CLS>()) VMBlock;
    result->SetClass(universe->GetBlockClassWithArgs(arguments));
    
    result->SetMethod(method);
    result->SetContext(context);
    
    LOG_ALLOCATION("VMBlock", result->GetObjectSize());
    return result;
}

VMClass* UniverseFactory::NewClass(VMClass* classOfClass) const {
    long numFields = classOfClass->GetNumberOfInstanceFields();
    VMClass* result;
    long additionalBytes = numFields * sizeof(VMObject*);
    if (numFields) result = new (GetHeap<HEAP_CLS>(), additionalBytes) VMClass(numFields);
    else result = new (GetHeap<HEAP_CLS>()) VMClass;
    
    result->SetClass(classOfClass);
    
    LOG_ALLOCATION("VMClass", result->GetObjectSize());
    return result;
}

VMDouble* UniverseFactory::NewDouble(double value) const {
    LOG_ALLOCATION("VMDouble", sizeof(VMDouble));
    return new (GetHeap<HEAP_CLS>()) VMDouble(value);
}

VMFrame* UniverseFactory::NewFrame(VMFrame* previousFrame, VMMethod* method) const {
    VMFrame* result = nullptr;
#ifdef UNSAFE_FRAME_OPTIMIZATION
    result = method->GetCachedFrame();
    if (result != nullptr) {
        method->SetCachedFrame(nullptr);
        result->SetPreviousFrame(previousFrame);
        return result;
    }
#endif
    long length = method->GetNumberOfArguments() +
    method->GetNumberOfLocals() +
    method->GetMaximumNumberOfStackElements();
    
    long additionalBytes = length * sizeof(VMObject*);
    result = new (GetHeap<HEAP_CLS>(), additionalBytes) VMFrame(length);
    result->clazz = nullptr;
# warning I think _store_ptr is sufficient here, but...
    result->method        = _store_ptr(method);
    result->previousFrame = _store_ptr(previousFrame);
    result->ResetStackPointer();
    
    LOG_ALLOCATION("VMFrame", result->GetObjectSize());
    return result;
}

VMObject* UniverseFactory::NewInstance(VMClass* classOfInstance) const {
    long numOfFields = classOfInstance->GetNumberOfInstanceFields();
    //the additional space needed is calculated from the number of fields
    long additionalBytes = numOfFields * sizeof(VMObject*);
    VMObject* result = new (GetHeap<HEAP_CLS>(), additionalBytes) VMObject(numOfFields);
    result->SetClass(classOfInstance);
    
    LOG_ALLOCATION(classOfInstance->GetName()->GetStdString(), result->GetObjectSize());
    return result;
}


VMInteger* UniverseFactory::NewInteger(int64_t value) const {
    
#ifdef GENERATE_INTEGER_HISTOGRAM
    integerHist[value/INT_HIST_SIZE] = integerHist[value/INT_HIST_SIZE]+1;
#endif
    
#if CACHE_INTEGER
    size_t index = (size_t) value - (size_t)INT_CACHE_MIN_VALUE;
    if (index < (size_t)(INT_CACHE_MAX_VALUE - INT_CACHE_MIN_VALUE)) {
        return static_cast<VMInteger*>(load_ptr(prebuildInts[index]));
    }
#endif
    
    LOG_ALLOCATION("VMInteger", sizeof(VMInteger));
    return new (GetHeap<HEAP_CLS>()) VMInteger(value);
}

VMClass* UniverseFactory::NewMetaclassClass() const {
    VMClass* result = new (GetHeap<HEAP_CLS>()) VMClass;
    result->SetClass(new (GetHeap<HEAP_CLS>()) VMClass);
    
    VMClass* mclass = result->GetClass();
    mclass->SetClass(result);
    
    LOG_ALLOCATION("VMClass", result->GetObjectSize());
    return result;
}

VMMethod* UniverseFactory::NewMethod( VMSymbol* signature,
                              size_t numberOfBytecodes, size_t numberOfConstants) const {
    //Method needs space for the bytecodes and the pointers to the constants
    long additionalBytes = PADDED_SIZE(numberOfBytecodes + numberOfConstants*sizeof(VMObject*));
    //#if GC_TYPE==GENERATIONAL
    //    VMMethod* result = new (GetHeap<HEAP_CLS>(),additionalBytes, true)
    //                VMMethod(numberOfBytecodes, numberOfConstants);
    //#else
    VMMethod* result = new (GetHeap<HEAP_CLS>(),additionalBytes)
    VMMethod(numberOfBytecodes, numberOfConstants);
    //#endif
    result->SetClass(load_ptr(methodClass));
    
    result->SetSignature(signature);
    
    LOG_ALLOCATION("VMMethod", result->GetObjectSize());
    return result;
}

VMString* UniverseFactory::NewString( const StdString& str) const {
    return NewString(str.c_str());
}

VMString* UniverseFactory::NewString( const char* str) const {
    VMString* result = new (GetHeap<HEAP_CLS>(), PADDED_SIZE(strlen(str) + 1)) VMString(str);
    
    LOG_ALLOCATION("VMString", result->GetObjectSize());
    return result;
}

VMSymbol* UniverseFactory::NewSymbol(const StdString& str) {
    return NewSymbol(str.c_str());
}

VMSymbol* UniverseFactory::NewSymbol(const char* str) {
    VMSymbol* result = new (GetHeap<HEAP_CLS>(), PADDED_SIZE(strlen(str)+1)) VMSymbol(str);
# warning is _store_ptr sufficient here?
    universe->symbolsMap[str] = _store_ptr(result);
    
    LOG_ALLOCATION("VMSymbol", result->GetObjectSize());
    return result;
}

VMClass* UniverseFactory::NewSystemClass() const {
    VMClass* systemClass = new (GetHeap<HEAP_CLS>()) VMClass();
    
    systemClass->SetClass(new (GetHeap<HEAP_CLS>()) VMClass());
    VMClass* mclass = systemClass->GetClass();
    
    mclass->SetClass(load_ptr(metaClassClass));
    
    LOG_ALLOCATION("VMClass", systemClass->GetObjectSize());
    return systemClass;
}
