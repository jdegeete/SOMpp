/*
 *
 *
 Copyright (c) 2007 Michael Haupt, Tobias Pape, Arne Bergmann
 Software Architecture Group, Hasso Plattner Institute, Potsdam, Germany
 http://www.hpi.uni-potsdam.de/swa/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include <sstream> 
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include <iomanip>

#include "Universe.h"
#include "Shell.h"

#include "../interpreter/Interpreter.h"
#include <primitives/Core.h>

#include <misc/debug.h>

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
#include <vmobjects/VMBigInteger.h>
#include <vmobjects/VMEvaluationPrimitive.h>

#include <natives/VMThread.h>
#include <natives/VMMutex.h>
#include <natives/VMSignal.h>

#include <interpreter/bytecodes.h>

#include <compiler/Disassembler.h>
#include <compiler/SourcecodeCompiler.h>

#ifdef USE_TAGGING
#include "../vmobjects/IntegerBox.h"
#endif

#include <vmobjects/VMBlock.inline.h>
#include <vmobjects/VMMethod.inline.h>

#ifdef CACHE_INTEGER
#ifndef INT_CACHE_MIN_VALUE
#define INT_CACHE_MIN_VALUE (-5)
#endif
#ifndef INT_CACHE_MAX_VALUE
#define INT_CACHE_MAX_VALUE (100)
#endif
pVMInteger prebuildInts[INT_CACHE_MAX_VALUE - INT_CACHE_MIN_VALUE + 1];
#endif

#define INT_HIST_SIZE 1

// Here we go:

short dumpBytecodes;
short gcVerbosity;

Universe* Universe::theUniverse = NULL;

GCObject* nilObject;
GCObject* trueObject;
GCObject* falseObject;

GCClass* objectClass;
GCClass* classClass;
GCClass* metaClassClass;

GCClass* nilClass;
GCClass* integerClass;
GCClass* bigIntegerClass;
GCClass* arrayClass;
GCClass* methodClass;
GCClass* symbolClass;
GCClass* primitiveClass;
GCClass* stringClass;
GCClass* systemClass;
GCClass* blockClass;
GCClass* doubleClass;

GCClass* trueClass;
GCClass* falseClass;

GCClass* threadClass;
GCClass* mutexClass;
GCClass* signalClass;

GCSymbol* symbolIfTrue;
GCSymbol* symbolIfFalse;

std::map<std::string, GCSymbol*> symbolsMap;

std::string bm_name;
#ifdef GENERATE_ALLOCATION_STATISTICS
struct alloc_data {long noObjects; long sizeObjects;};
std::map<std::string, struct alloc_data> allocationStats;
#define LOG_ALLOCATION(TYPE,SIZE) {struct alloc_data tmp=allocationStats[TYPE];tmp.noObjects++;tmp.sizeObjects+=(SIZE);allocationStats[TYPE]=tmp;}
#endif

map<long, long> integerHist;

void Universe::Start(long argc, char** argv) {
    theUniverse = new Universe();
    theUniverse->initialize(argc, argv);
}

void Universe::Quit(long err) {
    cout << "Time spent in GC: [" << Timer::GCTimer->GetTotalTime() << "] msec"
            << endl;
#if GC_TYPE!=PAUSELESS
    cout << "Number of GC cycles performed: " << _HEAP->GetNumberOfCycles() << endl;
#else
    cout <<  "Number of GC cycles performed: " << _HEAP->GetCycle() << endl;
#endif
#ifdef GENERATE_INTEGER_HISTOGRAM
    std::string file_name_hist = std::string(bm_name);
    file_name_hist.append("_integer_histogram.csv");
    fstream hist_csv(file_name_hist.c_str(), ios::out);

    for (map<long, long>::iterator it = integerHist.begin(); it != integerHist.end(); it++) {
        hist_csv << it->first << ", " << it->second << endl;
    }
#endif

#ifdef LOG_RECEIVER_TYPES
    std::string file_name_receivers = std::string(bm_name);
    file_name_receivers.append("_receivers.csv");
    fstream receivers(file_name_receivers.c_str(), ios::out);
    for (map<StdString, long>::iterator it = theUniverse->receiverTypes.begin(); it != theUniverse->receiverTypes.end(); it++)
    receivers << it->first << ",  " << it->second << endl;

    std::string file_name_send_types = std::string(bm_name);
    file_name_send_types.append("_send_types.csv");
    fstream send_stat(file_name_send_types.c_str(), ios::out);
    send_stat << "#name, percentage_primitive_calls, no_primitive_calls, no_non_primitive_calls" << endl;
    for (map<StdString, Universe::stat_data>::iterator it = theUniverse->callStats.begin(); it != theUniverse->callStats.end(); it++)
    send_stat << it->first << ", " << setiosflags(ios::fixed) << setprecision(2) << (double)(it->second.noPrimitiveCalls) / (double)(it->second.noCalls) <<
    ", " << it->second.noPrimitiveCalls << ", " << it->second.noCalls - it->second.noPrimitiveCalls << endl;
#endif

#ifdef GENERATE_ALLOCATION_STATISTICS
    std::string file_name_allocation = std::string(bm_name);
    file_name_allocation.append("_allocation_statistics.csv");

    fstream file_alloc_stats(file_name_allocation.c_str(), ios::out);
    map<std::string, struct alloc_data>::iterator iter;
    for (iter = allocationStats.begin(); iter != allocationStats.end(); iter++)
    {
        file_alloc_stats << iter->first << ", " << iter->second.noObjects << ", " << iter->second.sizeObjects << std::endl;
    }
#endif
    //if (theUniverse)
    //    delete (theUniverse);

    exit((int) err);
}

void Universe::ErrorExit(const char* err) {
    cout << "Runtime error: " << err << endl;
    Quit(ERR_FAIL);
}

vector<StdString> Universe::handleArguments(long argc, char** argv) {
    vector<StdString> vmArgs = vector<StdString>();
    dumpBytecodes = 0;
    gcVerbosity   = 0;

    for (long i = 1; i < argc; ++i) {

        if (strncmp(argv[i], "-cp", 3) == 0) {
            if ((argc == i + 1) || classPath.size() > 0)
                printUsageAndExit(argv[0]);
            setupClassPath(StdString(argv[++i]));
        } else if (strncmp(argv[i], "-d", 2) == 0) {
            ++dumpBytecodes;
        } else if (strncmp(argv[i], "-g", 2) == 0) {
            ++gcVerbosity;
        } else if (strncmp(argv[i], "-H", 2) == 0) {
            long heap_size = 0;
            char unit[3];
            if (sscanf(argv[i], "-H%ld%2s", &heap_size, unit) == 2) {
                if (strcmp(unit, "KB") == 0)
                    heapSize = heap_size * 1024;
                else if (strcmp(unit, "MB") == 0)
                    heapSize = heap_size * 1024 * 1024;
            } else
                printUsageAndExit(argv[0]);

        } else if ((strncmp(argv[i], "-h", 2) == 0)
                || (strncmp(argv[i], "--help", 6) == 0)) {
            printUsageAndExit(argv[0]);
        } else {
            vector<StdString> extPathTokens = vector<StdString>(2);
            StdString tmpString = StdString(argv[i]);
            if (this->getClassPathExt(extPathTokens, tmpString) ==
            ERR_SUCCESS) {
                this->addClassPath(extPathTokens[0]);
            }
            //Different from CSOM!!!:
            //In CSOM there is an else, where the original filename is pushed into the vm_args.
            //But unlike the class name in extPathTokens (extPathTokens[1]) that could
            //still have the .som suffix though.
            //So in SOM++ getClassPathExt will strip the suffix and add it to extPathTokens
            //even if there is no new class path present. So we can in any case do the following:
            vmArgs.push_back(extPathTokens[1]);
        }
    }
    addClassPath(StdString("."));

    return vmArgs;
}

long Universe::getClassPathExt(vector<StdString>& tokens,
        const StdString& arg) const {
#define EXT_TOKENS 2
    long result = ERR_SUCCESS;
    long fpIndex = arg.find_last_of(fileSeparator);
    long ssepIndex = arg.find(".som");

    if (fpIndex == StdString::npos) { //no new path
        //different from CSOM (see also HandleArguments):
        //we still want to strip the suffix from the filename, so
        //we set the start to -1, in order to start the substring
        //from character 0. npos is -1 too, but this is to make sure
        fpIndex = -1;
        //instead of returning here directly, we have to remember that
        //there is no new class path and return it later
        result = ERR_FAIL;
    } else
        tokens[0] = arg.substr(0, fpIndex);

    //adding filename (minus ".som" if present) to second slot
    ssepIndex =
            ((ssepIndex != StdString::npos) && (ssepIndex > fpIndex)) ?
                    (ssepIndex - 1) : arg.length();
    tokens[1] = arg.substr(fpIndex + 1, ssepIndex - (fpIndex));
    return result;
}

long Universe::setupClassPath(const StdString& cp) {
    try {
        std::stringstream ss(cp);
        StdString token;

        long i = 0;
        while (getline(ss, token, pathSeparator)) {
            classPath.push_back(token);
            ++i;
        }

        return ERR_SUCCESS;
    } catch (std::exception e) {
        return ERR_FAIL;
    }
}

long Universe::addClassPath(const StdString& cp) {
    classPath.push_back(cp);
    return ERR_SUCCESS;
}

void Universe::printUsageAndExit(char* executable) const {
    cout << "Usage: " << executable << " [-options] [args...]" << endl << endl;
    cout << "where options include:" << endl;
    cout << "    -cp <directories separated by " << pathSeparator << ">"
            << endl;
    cout << "        set search path for application classes" << endl;
    cout << "    -d  enable disassembling (twice for tracing)" << endl;
    cout << "    -g  enable garbage collection details:" << endl
            << "        1x - print statistics when VM shuts down" << endl
            << "        2x - print statistics upon each collection" << endl
            << "        3x - print statistics and dump _HEAP upon each " << endl
            << "collection" << endl;
    cout << "    -HxMB set the _HEAP size to x MB (default: 1 MB)" << endl;
    cout << "    -HxKB set the _HEAP size to x KB (default: 1 MB)" << endl;
    cout << "    -h  show this help" << endl;

    Quit(ERR_SUCCESS);
}

Universe::Universe() {
    pthread_key_create(&interpreterKey, NULL);
    pthread_mutex_init(&interpreterMutex, NULL);
    pthread_mutexattr_init(&attrclassLoading);
    pthread_mutexattr_settype(&attrclassLoading, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&classLoading, &attrclassLoading);
    
    pthread_mutex_init(&testMutex, NULL);
}

void Universe::initialize(long _argc, char** _argv) {
#ifdef GENERATE_ALLOCATION_STATISTICS
    allocationStats["VMArray"] = {0,0};
#endif

    //heapSize = 240 * 1024 * 1024;
    //pageSize = 4 * 8192;

    vector<StdString> argv = this->handleArguments(_argc, _argv);
    
    // remember file that was executed (for writing statistics)
    if (argv.size() > 0)
        bm_name = argv[0];

    PagedHeap::InitializeHeap(HEAP_SIZE, PAGE_SIZE);
    setup_primitives();

    heap = _HEAP;

    interpreters = vector<Interpreter*>();
    Interpreter* interpreter = this->NewInterpreter();

#ifdef CACHE_INTEGER
    //create prebuilt integers
    for (long it = INT_CACHE_MIN_VALUE; it <= INT_CACHE_MAX_VALUE; ++it) {
#if GC_TYPE==GENERATIONAL
        prebuildInts[(unsigned long)(it - INT_CACHE_MIN_VALUE)] = new (_HEAP, _PAGE) VMInteger(it);
#elif GC_TYPE==PAUSELESS
        prebuildInts[(unsigned long)(it - INT_CACHE_MIN_VALUE)] = new (_HEAP, _UNIVERSE->GetInterpreter()) VMInteger(it);
#else
        prebuildInts[(unsigned long)(it - INT_CACHE_MIN_VALUE)] = new (_HEAP) VMInteger(it);
#endif
    }
#endif

    InitializeGlobals();

    pVMObject systemObject = NewInstance(READBARRIER(systemClass));

    this->SetGlobal(SymbolForChars("nil"),    READBARRIER(nilObject));
    this->SetGlobal(SymbolForChars("true"),   READBARRIER(trueObject));
    this->SetGlobal(SymbolForChars("false"),  READBARRIER(falseObject));
    this->SetGlobal(SymbolForChars("system"), systemObject);
    this->SetGlobal(SymbolForChars("System"), READBARRIER(systemClass));
    this->SetGlobal(SymbolForChars("Block"),  READBARRIER(blockClass));

    symbolIfTrue  = WRITEBARRIER(SymbolForChars("ifTrue:"));
    symbolIfFalse = WRITEBARRIER(SymbolForChars("ifFalse:"));

    pVMMethod bootstrapMethod = NewMethod(SymbolForChars("bootstrap"), 1, 0);
    bootstrapMethod->SetBytecode(0, BC_HALT);
    bootstrapMethod->SetNumberOfLocals(0);

    bootstrapMethod->SetMaximumNumberOfStackElements(2);
    bootstrapMethod->SetHolder(READBARRIER(systemClass));
    
    pVMThread thread = NewThread();
    pVMSignal signal = NewSignal();
    thread->SetResumeSignal(signal);
    thread->SetShouldStop(false);
    interpreter->SetThread(thread);

    if (argv.size() == 0) {
        Shell* shell = new Shell(bootstrapMethod);
        shell->Start();
        return;
    }

    /* only trace bootstrap if the number of cmd-line "-d"s is > 2 */
    short trace = 2 - dumpBytecodes;
    if (!(trace > 0))
        dumpBytecodes = 1;

    pVMArray argumentsArray = NewArrayFromStrings(argv);

    pVMFrame bootstrapFrame = interpreter->PushNewFrame(bootstrapMethod);
    bootstrapFrame->Push(systemObject);
    bootstrapFrame->Push(argumentsArray);

    pVMInvokable initialize =
        static_cast<pVMInvokable>(READBARRIER(systemClass)->LookupInvokable(this->SymbolForChars("initialize:")));
    (*initialize)(bootstrapFrame);

    // reset "-d" indicator
    if (!(trace > 0))
        dumpBytecodes = 2 - trace;
    
#if GC_TYPE==PAUSELESS
    _HEAP->Start();
#endif
    interpreter->Start();
    //pthread_exit(0);
}

Universe::~Universe() {
    pthread_key_delete(interpreterKey);
    pthread_mutex_destroy(&interpreterMutex);
    PagedHeap::DestroyHeap();
}

#ifdef NDEBUG
    bool Universe::IsValidObject(const pVMObject const obj) {
        return true;
    }
    static void set_vt_to_null() {}
    static void obtain_vtables_of_known_classes(pVMSymbol className) {}
#else
    void* vt_array;
    void* vt_biginteger;
    void* vt_block;
    void* vt_class;
    void* vt_double;
    void* vt_eval_primitive;
    void* vt_frame;
    void* vt_integer;
    void* vt_method;
    void* vt_object;
    void* vt_primitive;
    void* vt_string;
    void* vt_symbol;
    void* vt_thread;
    void* vt_mutex;
    void* vt_signal;

    bool Universe::IsValidObject(const pVMObject const obj) {
        if (obj == (pVMObject) INVALID_VM_POINTER
            // || obj == nullptr
            ) {
            assert(false);
            return false;
        }
        
        if (obj == nullptr)
            return true;
        
        
        if (vt_symbol == nullptr) // initialization not yet completed
            return true;
        
        void* vt = *(void**) obj;
        bool b = vt == vt_array    ||
               vt == vt_biginteger ||
               vt == vt_block      ||
               vt == vt_class      ||
               vt == vt_double     ||
               vt == vt_eval_primitive ||
               vt == vt_frame      ||
               vt == vt_integer    ||
               vt == vt_method     ||
               vt == vt_object     ||
               vt == vt_primitive  ||
               vt == vt_string     ||
               vt == vt_symbol     ||
               vt == vt_thread     ||
               vt == vt_mutex      ||
               vt == vt_signal;
        assert(b);
        return b;
    }

    static void set_vt_to_null() {
        vt_array      = nullptr;
        vt_biginteger = nullptr;
        vt_block      = nullptr;
        vt_class      = nullptr;
        vt_double     = nullptr;
        vt_eval_primitive = nullptr;
        vt_frame      = nullptr;
        vt_integer    = nullptr;
        vt_method     = nullptr;
        vt_object     = nullptr;
        vt_primitive  = nullptr;
        vt_string     = nullptr;
        vt_symbol     = nullptr;
        vt_thread     = nullptr;
        vt_mutex      = nullptr;
        vt_signal     = nullptr;
    }

    static void obtain_vtables_of_known_classes(pVMSymbol className) {
#if GC_TYPE==GENERATIONAL
        pVMArray arr  = new (_HEAP, _PAGE) VMArray(0, 0);
#elif GC_TYPE==PAUSELESS
        pVMArray arr  = new (_HEAP, _UNIVERSE->GetInterpreter()) VMArray(0, 0);
#else
        pVMArray arr  = new (_HEAP) VMArray(0, 0);
#endif
        vt_array      = *(void**) arr;
        
#if GC_TYPE==GENERATIONAL
        pVMBigInteger bi = new (_HEAP, _PAGE) VMBigInteger();
#elif GC_TYPE==PAUSELESS
        pVMBigInteger bi = new (_HEAP, _UNIVERSE->GetInterpreter()) VMBigInteger();
#else
        pVMBigInteger bi = new (_HEAP) VMBigInteger();
#endif
        vt_biginteger = *(void**) bi;
        
#if GC_TYPE==GENERATIONAL
        pVMBlock blck = new (_HEAP, _PAGE) VMBlock();
#elif GC_TYPE==PAUSELESS
        pVMBlock blck = new (_HEAP, _UNIVERSE->GetInterpreter()) VMBlock();
#else
        pVMBlock blck = new (_HEAP) VMBlock();
#endif
        vt_block      = *(void**) blck;
        
        vt_class      = *(void**) symbolClass;
        
#if GC_TYPE==GENERATIONAL
        pVMDouble dbl = new (_HEAP, _PAGE) VMDouble();
#elif GC_TYPE==PAUSELESS
        pVMDouble dbl = new (_HEAP, _UNIVERSE->GetInterpreter()) VMDouble();
#else
        pVMDouble dbl = new (_HEAP) VMDouble();
#endif
        vt_double     = *(void**) dbl;
        
#if GC_TYPE==GENERATIONAL
        VMEvaluationPrimitive* ev = new (_HEAP, _PAGE) VMEvaluationPrimitive(1);
#elif GC_TYPE==PAUSELESS
        VMEvaluationPrimitive* ev = new (_HEAP, _UNIVERSE->GetInterpreter()) VMEvaluationPrimitive(1);
#else
        VMEvaluationPrimitive* ev = new (_HEAP) VMEvaluationPrimitive(1);
#endif
        vt_eval_primitive = *(void**) ev;
        
#if GC_TYPE==GENERATIONAL
        pVMFrame frm  = new (_HEAP, _PAGE) VMFrame(0, 0);
#elif GC_TYPE==PAUSELESS
        pVMFrame frm  = new (_HEAP, _UNIVERSE->GetInterpreter()) VMFrame(0, 0);
#else
        pVMFrame frm  = new (_HEAP) VMFrame(0, 0);
#endif
        vt_frame      = *(void**) frm;
        
#if GC_TYPE==GENERATIONAL
        pVMInteger i  = new (_HEAP, _PAGE) VMInteger();
#elif GC_TYPE==PAUSELESS
        pVMInteger i  = new (_HEAP, _UNIVERSE->GetInterpreter()) VMInteger();
#else
        pVMInteger i  = new (_HEAP) VMInteger();
#endif
        vt_integer    = *(void**) i;
        
#if GC_TYPE==GENERATIONAL
        pVMMethod mth = new (_HEAP, _PAGE) VMMethod(0, 0, 0);
#elif GC_TYPE==PAUSELESS
        pVMMethod mth = new (_HEAP, _UNIVERSE->GetInterpreter()) VMMethod(0, 0, 0);
#else
        pVMMethod mth = new (_HEAP) VMMethod(0, 0, 0);
#endif
        vt_method     = *(void**) mth;
        vt_object     = *(void**) nilObject;
        
#if GC_TYPE==GENERATIONAL
        pVMPrimitive prm = new (_HEAP, _PAGE) VMPrimitive(className);
#elif GC_TYPE==PAUSELESS
        pVMPrimitive prm = new (_HEAP, _UNIVERSE->GetInterpreter()) VMPrimitive(className);
#else
        pVMPrimitive prm = new (_HEAP) VMPrimitive(className);
#endif
        vt_primitive  = *(void**) prm;
        
#if GC_TYPE==GENERATIONAL
        pVMString str = new (_HEAP, _PAGE, PADDED_SIZE(7)) VMString("foobar");
#elif GC_TYPE==PAUSELESS
        pVMString str = new (_HEAP, _UNIVERSE->GetInterpreter(), PADDED_SIZE(7)) VMString("foobar");
#else
        pVMString str = new (_HEAP, PADDED_SIZE(7)) VMString("foobar");
#endif
        vt_string     = *(void**) str;
        vt_symbol     = *(void**) className;
        
#if GC_TYPE==GENERATIONAL
        pVMThread thr = new (_HEAP, _PAGE) VMThread();
#elif GC_TYPE==PAUSELESS
        pVMThread thr = new (_HEAP, _UNIVERSE->GetInterpreter()) VMThread();
#else
        pVMThread thr = new (_HEAP) VMThread();
#endif
        vt_thread     = *(void**) thr;
        
#if GC_TYPE==GENERATIONAL
        pVMMutex mtx  = new (_HEAP, _PAGE) VMMutex();
#elif GC_TYPE==PAUSELESS
        pVMMutex mtx  = new (_HEAP, _UNIVERSE->GetInterpreter()) VMMutex();
#else
        pVMMutex mtx  = new (_HEAP) VMMutex();
#endif
        vt_mutex      = *(void**) mtx;
        
#if GC_TYPE==GENERATIONAL
        pVMSignal sgnl = new (_HEAP, _PAGE) VMSignal();
#elif GC_TYPE==PAUSELESS
        pVMSignal sgnl = new (_HEAP, _UNIVERSE->GetInterpreter()) VMSignal();
#else
        pVMSignal sgnl = new (_HEAP) VMSignal();
#endif
        vt_signal      = *(void**) sgnl;
    }
#endif

void Universe::InitializeGlobals() {
    set_vt_to_null();
    
    //
    //allocate nil object
    //
    
#if GC_TYPE==GENERATIONAL
    nilObject = WRITEBARRIER(new (_HEAP, _PAGE) VMObject);
#elif GC_TYPE==PAUSELESS
    nilObject = WRITEBARRIER(new (_HEAP, _UNIVERSE->GetInterpreter()) VMObject);
#else
    nilObject = new (_HEAP) VMObject;
#endif
    
    static_cast<VMObject*>(READBARRIER(nilObject))->SetField(0, READBARRIER(nilObject));

    metaClassClass = WRITEBARRIER(NewMetaclassClass());

    objectClass     = WRITEBARRIER(NewSystemClass());
    nilClass        = WRITEBARRIER(NewSystemClass());
    classClass      = WRITEBARRIER(NewSystemClass());
    arrayClass      = WRITEBARRIER(NewSystemClass());
    symbolClass     = WRITEBARRIER(NewSystemClass());
    methodClass     = WRITEBARRIER(NewSystemClass());
    integerClass    = WRITEBARRIER(NewSystemClass());
    bigIntegerClass = WRITEBARRIER(NewSystemClass());
    primitiveClass  = WRITEBARRIER(NewSystemClass());
    stringClass     = WRITEBARRIER(NewSystemClass());
    doubleClass     = WRITEBARRIER(NewSystemClass());
    threadClass     = WRITEBARRIER(NewSystemClass());
    mutexClass      = WRITEBARRIER(NewSystemClass());
    signalClass     = WRITEBARRIER(NewSystemClass());

    READBARRIER(nilObject)->SetClass(READBARRIER(nilClass));

    InitializeSystemClass(READBARRIER(objectClass),    nullptr, "Object");
    InitializeSystemClass(READBARRIER(classClass),     READBARRIER(objectClass), "Class");
    InitializeSystemClass(READBARRIER(metaClassClass), READBARRIER(classClass), "Metaclass");
    InitializeSystemClass(READBARRIER(nilClass),    READBARRIER(objectClass), "Nil");
    InitializeSystemClass(READBARRIER(arrayClass),  READBARRIER(objectClass), "Array");
    InitializeSystemClass(READBARRIER(methodClass), READBARRIER(arrayClass), "Method");
    InitializeSystemClass(READBARRIER(symbolClass), READBARRIER(objectClass), "Symbol");
    InitializeSystemClass(READBARRIER(integerClass), READBARRIER(objectClass), "Integer");
    InitializeSystemClass(READBARRIER(bigIntegerClass), READBARRIER(objectClass),
            "BigInteger");
    InitializeSystemClass(READBARRIER(primitiveClass), READBARRIER(objectClass),
            "Primitive");
    InitializeSystemClass(READBARRIER(stringClass), READBARRIER(objectClass), "String");
    InitializeSystemClass(READBARRIER(doubleClass), READBARRIER(objectClass), "Double");
    InitializeSystemClass(READBARRIER(threadClass), READBARRIER(objectClass), "Thread");
    InitializeSystemClass(READBARRIER(mutexClass), READBARRIER(objectClass), "Mutex");
    InitializeSystemClass(READBARRIER(signalClass), READBARRIER(objectClass), "Signal");

    // Fix up objectClass
    READBARRIER(objectClass)->SetSuperClass((pVMClass) READBARRIER(nilObject));

    LoadSystemClass(READBARRIER(objectClass));
    LoadSystemClass(READBARRIER(classClass));
    LoadSystemClass(READBARRIER(metaClassClass));
    LoadSystemClass(READBARRIER(nilClass));
    LoadSystemClass(READBARRIER(arrayClass));
    LoadSystemClass(READBARRIER(methodClass));
    LoadSystemClass(READBARRIER(symbolClass));
    LoadSystemClass(READBARRIER(integerClass));
    LoadSystemClass(READBARRIER(bigIntegerClass));
    LoadSystemClass(READBARRIER(primitiveClass));
    LoadSystemClass(READBARRIER(stringClass));
    LoadSystemClass(READBARRIER(doubleClass));
    LoadSystemClass(READBARRIER(threadClass));
    LoadSystemClass(READBARRIER(mutexClass));
    LoadSystemClass(READBARRIER(signalClass));

    blockClass = WRITEBARRIER(LoadClass(SymbolForChars("Block")));

    pVMSymbol trueClassName = SymbolForChars("True");
    trueClass  = WRITEBARRIER(LoadClass(trueClassName));
    trueObject = (GCObject*) WRITEBARRIER(NewInstance(READBARRIER(trueClass)));
    
    pVMSymbol falseClassName = SymbolForChars("False");
    falseClass  = WRITEBARRIER(LoadClass(falseClassName));
    falseObject = (GCObject*) WRITEBARRIER(NewInstance(READBARRIER(falseClass)));

    systemClass = WRITEBARRIER(LoadClass(SymbolForChars("System")));

    obtain_vtables_of_known_classes(falseClassName);
}

void Universe::Assert(bool value) const {
    if (!value) {
        cout << "Assertion failed" << endl;
    }

}

pVMClass Universe::GetBlockClass() const {
    return READBARRIER(blockClass);
}

pVMClass Universe::GetBlockClassWithArgs(long numberOfArguments) {
    map<long, GCClass*>::iterator it =
    blockClassesByNoOfArgs.find(numberOfArguments);
    if (it != blockClassesByNoOfArgs.end()) {
        return READBARRIER(it->second);
    }

    this->Assert(numberOfArguments < 10);

    ostringstream Str;
    Str << "Block" << numberOfArguments;
    pVMSymbol name = SymbolFor(Str.str());
    pVMClass result = LoadClassBasic(name, NULL);
    
#if GC_TYPE==GENERATIONAL
    result->AddInstancePrimitive(new (_HEAP, _PAGE) VMEvaluationPrimitive(numberOfArguments) );
#elif GC_TYPE==PAUSELESS
    result->AddInstancePrimitive(new (_HEAP, _UNIVERSE->GetInterpreter(), 0, true) VMEvaluationPrimitive(numberOfArguments) );
#else
    result->AddInstancePrimitive(new (_HEAP) VMEvaluationPrimitive(numberOfArguments) );
#endif

    SetGlobal(name, result);
    blockClassesByNoOfArgs[numberOfArguments] = WRITEBARRIER(result);

    return result;
}

#if GC_TYPE==PAUSELESS
pVMObject Universe::GetGlobal(pVMSymbol name) {
    pthread_mutex_lock(&testMutex);
    map<GCSymbol*, GCAbstractObject*>::iterator it;
    it = globals.find((GCSymbol*) name);
    if (it == globals.end()) {
        it = globals.find(Flip((GCSymbol*) name));
    }
    if (it == globals.end()) {
        pthread_mutex_unlock(&testMutex);
        return nullptr;
    } else {
        pthread_mutex_unlock(&testMutex);
        return READBARRIER(it->second);
    }
}
#else
pVMObject Universe::GetGlobal(pVMSymbol name) {
    map<GCSymbol*, GCAbstractObject*>::iterator it;
    it = globals.find((GCSymbol*) name);
    if (it == globals.end())
        return nullptr;
    else
        return READBARRIER(it->second);
}
#endif

void Universe::InitializeSystemClass(pVMClass systemClass,
pVMClass superClass, const char* name) {
    StdString s_name(name);

    if (superClass != NULL) {
        systemClass->SetSuperClass(superClass);
        pVMClass sysClassClass = systemClass->GetClass();
        pVMClass superClassClass = superClass->GetClass();
        sysClassClass->SetSuperClass(superClassClass);
    } else {
        pVMClass sysClassClass = systemClass->GetClass();
        sysClassClass->SetSuperClass(READBARRIER(classClass));
    }

    pVMClass sysClassClass = systemClass->GetClass();

    systemClass->SetInstanceFields(NewArray(0));
    sysClassClass->SetInstanceFields(NewArray(0));

    systemClass->SetInstanceInvokables(NewArray(0));
    sysClassClass->SetInstanceInvokables(NewArray(0));

    systemClass->SetName(SymbolFor(s_name));
    ostringstream Str;
    Str << s_name << " class";
    StdString classClassName(Str.str());
    sysClassClass->SetName(SymbolFor(classClassName));

    SetGlobal(systemClass->GetName(), systemClass);
}

pVMClass Universe::LoadClass(pVMSymbol name) {
    pthread_mutex_lock(&classLoading);
    pVMClass result = static_cast<pVMClass>(GetGlobal(name));
    
    if (result != nullptr) {
        pthread_mutex_unlock(&classLoading);
        return result;
    }
    
    result = LoadClassBasic(name, NULL);

    if (!result) {
		// we fail silently, it is not fatal that loading a class failed
        pthread_mutex_unlock(&classLoading);
		return (pVMClass) READBARRIER(nilObject);
    }

    if (result->HasPrimitives() || result->GetClass()->HasPrimitives())
        result->LoadPrimitives();
    
    SetGlobal(name, result);

    pthread_mutex_unlock(&classLoading);
    return result;
}

pVMClass Universe::LoadClassBasic(pVMSymbol name, pVMClass systemClass) {
    
    
    StdString s_name = name->GetStdString();
    //sync_out(ostringstream() << "LoadClassBasic: " << name->GetChars());
    // assert(0 != strcmp(name->GetChars(), "nil")); // NOTE: name can be nil. During assembling we do a load again, unconditionally, also for nil symbol. Should be fixed...
    
    pVMClass result;

    for (vector<StdString>::iterator i = classPath.begin();
            i != classPath.end(); ++i) {
        SourcecodeCompiler compiler;
        result = compiler.CompileClass(*i, name->GetStdString(), systemClass);
        if (result) {
            if (dumpBytecodes) {
                Disassembler::Dump(result->GetClass());
                Disassembler::Dump(result);
            }
            return result;
        }

    }
    return NULL;
}

pVMClass Universe::LoadShellClass( StdString& stmt) {
    SourcecodeCompiler compiler;
    pVMClass result = compiler.CompileClassString(stmt, NULL);
    if(dumpBytecodes)
        Disassembler::Dump(result);
    return result;
}

void Universe::LoadSystemClass( pVMClass systemClass) {
    pVMClass result = LoadClassBasic(systemClass->GetName(), systemClass);
    StdString s = systemClass->GetName()->GetStdString();

    if (!result) {
        cout << "Can't load system class: " << s << endl;
        Universe::Quit(ERR_FAIL);
    }

    if (result->HasPrimitives() || result->GetClass()->HasPrimitives())
    result->LoadPrimitives();
}

pVMArray Universe::NewArray(long size) const {
    long additionalBytes = size * sizeof(pVMObject);
    
#if GC_TYPE==GENERATIONAL
    // if the array is too big for the nursery, we will directly allocate a
    // mature object
    bool outsideNursery = additionalBytes + sizeof(VMArray) > _HEAP->GetMaxObjectSize();

    pVMArray result = new (_HEAP, _PAGE, additionalBytes, outsideNursery) VMArray(size);
    if (outsideNursery)
        result->SetGCField(MASK_OBJECT_IS_OLD);
#elif GC_TYPE==PAUSELESS
    pVMArray result = new (_HEAP, _UNIVERSE->GetInterpreter(), additionalBytes) VMArray(size);
#else
    pVMArray result = new (_HEAP, additionalBytes) VMArray(size);
#endif

    result->SetClass(READBARRIER(arrayClass));
    
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMArray", result->GetObjectSize());
#endif
    
    return result;
}

pVMArray Universe::NewArrayFromStrings(const vector<StdString>& argv) const {
    pVMArray result = NewArray(argv.size());
    long j = 0;
    for (vector<StdString>::const_iterator i = argv.begin();
            i != argv.end(); ++i) {
        result->SetIndexableField(j, NewString(*i));
        ++j;
    }

    return result;
}

pVMArray Universe::NewArrayList(ExtendedList<pVMSymbol>& list) const {
    ExtendedList<pVMObject>& objList = (ExtendedList<pVMObject>&) list;
    return NewArrayList(objList);
}

pVMArray Universe::NewArrayList(ExtendedList<pVMObject>& list) const {
    long size = list.Size();
    pVMArray result = NewArray(size);

    if (result) {
        for (long i = 0; i < size; ++i) {
            pVMObject elem = list.Get(i);
            result->SetIndexableField(i, elem);
        }
    }
    return result;
}

pVMBigInteger Universe::NewBigInteger( int64_t value) const {
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMBigInteger", sizeof(VMBigInteger));
#endif
#if GC_TYPE==GENERATIONAL
    return new (_HEAP, _PAGE) VMBigInteger(value);
#elif GC_TYPE==PAUSELESS
    return new (_HEAP, _UNIVERSE->GetInterpreter()) VMBigInteger(value);
#else
    return new (_HEAP) VMBigInteger(value);
#endif
}

pVMBlock Universe::NewBlock(pVMMethod method, pVMFrame context, long arguments) {
#if GC_TYPE==GENERATIONAL
    pVMBlock result = new (_HEAP, _PAGE) VMBlock;
#elif GC_TYPE==PAUSELESS
    pVMBlock result = new (_HEAP, _UNIVERSE->GetInterpreter()) VMBlock;
#else
    pVMBlock result = new (_HEAP) VMBlock;
#endif
    result->SetClass(this->GetBlockClassWithArgs(arguments));

    result->SetMethod(method);
    result->SetContext(context);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMBlock", result->GetObjectSize());
#endif

    return result;
}

pVMClass Universe::NewClass(pVMClass classOfClass) const {
    long numFields = classOfClass->GetNumberOfInstanceFields();
    pVMClass result;
    long additionalBytes = numFields * sizeof(pVMObject);
    if (numFields)
#if GC_TYPE==GENERATIONAL
    result = new (_HEAP, _PAGE, additionalBytes) VMClass(numFields);
#elif GC_TYPE==PAUSELESS
    result = new (_HEAP, _UNIVERSE->GetInterpreter(), additionalBytes, true) VMClass(numFields);
#else
    result = new (_HEAP, additionalBytes) VMClass(numFields);
#endif
    else
#if GC_TYPE==GENERATIONAL
        result = new (_HEAP, _PAGE) VMClass;
#elif GC_TYPE==PAUSELESS
        result = new (_HEAP, _UNIVERSE->GetInterpreter(), 0, true) VMClass;
#else
        result = new (_HEAP) VMClass;
#endif

    result->SetClass(classOfClass);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMClass", result->GetObjectSize());
#endif

    return result;
}

pVMDouble Universe::NewDouble(double value) const {
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMDouble", sizeof(VMDouble));
#endif
#if GC_TYPE==GENERATIONAL
    return new (_HEAP, _PAGE) VMDouble(value);
#elif GC_TYPE==PAUSELESS
    return new (_HEAP, _UNIVERSE->GetInterpreter()) VMDouble(value);
#else
    return new (_HEAP) VMDouble(value);
#endif
}

pVMFrame Universe::NewFrame(pVMFrame previousFrame, pVMMethod method) const {
    pVMFrame result = NULL;
    
    /*
#ifdef UNSAFE_FRAME_OPTIMIZATION
    result = method->GetCachedFrame();
    if (result != NULL) {
        method->SetCachedFrame(NULL);
        result->SetPreviousFrame(previousFrame);
        return result;
    }
#endif
    */
    
    long length = method->GetNumberOfArguments() +
                  method->GetNumberOfLocals() +
                  method->GetMaximumNumberOfStackElements();

    long additionalBytes = length * sizeof(pVMObject);
#if GC_TYPE==GENERATIONAL
    result = new (_HEAP, _PAGE, additionalBytes) VMFrame(length);
#elif GC_TYPE==PAUSELESS
    result = new (_HEAP, _UNIVERSE->GetInterpreter(), additionalBytes) VMFrame(length);
#else
    result = new (_HEAP, additionalBytes) VMFrame(length);
#endif
    result->clazz = nullptr;
    result->method = WRITEBARRIER(method);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMFrame", result->GetObjectSize());
#endif
    result->previousFrame = WRITEBARRIER(previousFrame);
    result->ResetStackPointer();
    return result;
}

pVMObject Universe::NewInstance( pVMClass classOfInstance) const {
    long numOfFields = classOfInstance->GetNumberOfInstanceFields();
    //the additional space needed is calculated from the number of fields
    long additionalBytes = numOfFields * sizeof(pVMObject);
#if GC_TYPE==GENERATIONAL
    pVMObject result = new (_HEAP, _PAGE, additionalBytes) VMObject(numOfFields);
#elif GC_TYPE==PAUSELESS
    pVMObject result = new (_HEAP, _UNIVERSE->GetInterpreter(), additionalBytes) VMObject(numOfFields);
#else
    pVMObject result = new (_HEAP, additionalBytes) VMObject(numOfFields);
#endif
    result->SetClass(classOfInstance);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION(classOfInstance->GetName()->GetStdString(), result->GetObjectSize());
#endif
    return result;
}

pVMInteger Universe::NewInteger( long value) const {

#ifdef GENERATE_INTEGER_HISTOGRAM
    integerHist[value/INT_HIST_SIZE] = integerHist[value/INT_HIST_SIZE]+1;
#endif

#ifdef CACHE_INTEGER
    unsigned long index = (unsigned long)value - (unsigned long)INT_CACHE_MIN_VALUE;
    if (index < (unsigned long)(INT_CACHE_MAX_VALUE - INT_CACHE_MIN_VALUE)) {
        return prebuildInts[index];
    }
#endif
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMInteger", sizeof(VMInteger));
#endif

#if GC_TYPE==GENERATIONAL
    return new (_HEAP, _PAGE) VMInteger(value);
#elif GC_TYPE==PAUSELESS
    return new (_HEAP, _UNIVERSE->GetInterpreter()) VMInteger(value);
#else
    return new (_HEAP) VMInteger(value);
#endif
}

pVMClass Universe::NewMetaclassClass() const {
#if GC_TYPE==GENERATIONAL
    pVMClass result = new (_HEAP, _PAGE) VMClass;
    result->SetClass(new (_HEAP, _PAGE) VMClass);
#elif GC_TYPE==PAUSELESS
    pVMClass result = new (_HEAP, _UNIVERSE->GetInterpreter(), 0, true) VMClass;
    result->SetClass(new (_HEAP, _UNIVERSE->GetInterpreter(), 0, true) VMClass);
#else
    pVMClass result = new (_HEAP) VMClass;
    result->SetClass(new (_HEAP) VMClass);
#endif
    pVMClass mclass = result->GetClass();
    mclass->SetClass(result);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMClass", result->GetObjectSize());
#endif

    return result;
}

#if GC_TYPE==PAUSELESS
void Universe::MarkGlobals() {
    ReadBarrierForGCThread(&nilObject, true);
    ReadBarrierForGCThread(&trueObject, true);
    ReadBarrierForGCThread(&falseObject, true);
    
    ReadBarrierForGCThread(&objectClass, true);
    ReadBarrierForGCThread(&classClass, true);
    ReadBarrierForGCThread(&metaClassClass, true);
    
    ReadBarrierForGCThread(&nilClass, true);
    ReadBarrierForGCThread(&integerClass, true);
    ReadBarrierForGCThread(&bigIntegerClass, true);
    ReadBarrierForGCThread(&arrayClass, true);
    ReadBarrierForGCThread(&methodClass, true);
    ReadBarrierForGCThread(&symbolClass, true);
    ReadBarrierForGCThread(&primitiveClass, true);
    ReadBarrierForGCThread(&stringClass, true);
    ReadBarrierForGCThread(&systemClass, true);
    ReadBarrierForGCThread(&blockClass, true);
    ReadBarrierForGCThread(&doubleClass, true);
    
    ReadBarrierForGCThread(&threadClass, true);
    ReadBarrierForGCThread(&mutexClass, true);
    ReadBarrierForGCThread(&signalClass, true);
    
    ReadBarrierForGCThread(&trueClass, true);
    ReadBarrierForGCThread(&falseClass, true);
    
    
    pthread_mutex_lock(&testMutex);
    
    // walk all entries in globals map
    map<GCSymbol*, GCAbstractObject*> globs;
    map<GCSymbol*, GCAbstractObject*>::iterator iter;
    for (iter = globals.begin(); iter != globals.end(); iter++) {
        pVMObject val = ReadBarrierForGCThread(&iter->second, true);
        if (val == NULL)
            continue;
        GCSymbol* key = iter->first;
        //sync_out(ostringstream() << "GLOB OLD: " << iter->first);
        //globs[key] = WriteBarrierForGCThread(val);
        
        GCSymbol* new_ptr = WriteBarrierForGCThread(ReadBarrierForGCThread(&key,true));
        globs[new_ptr] = WriteBarrierForGCThread(val);
        
        //sync_out(ostringstream() << "GLOB NEW: " << new_ptr);
    }
    globals = globs;
    
    //cout << "Mark symbol map" << endl;
    // walk all entries in symbols map
    map<StdString, GCSymbol*>::iterator symbolIter;
    for (symbolIter = symbolsMap.begin();
         symbolIter != symbolsMap.end();
         symbolIter++) {
        //insert overwrites old entries inside the internal map
        symbolIter->second = WriteBarrierForGCThread(ReadBarrierForGCThread(&symbolIter->second, true));
    }
    //cout << "Mark block classes" << endl;
    map<long, GCClass*>::iterator bcIter;
    for (bcIter = blockClassesByNoOfArgs.begin();
         bcIter != blockClassesByNoOfArgs.end();
         bcIter++) {
        bcIter->second = WriteBarrierForGCThread(ReadBarrierForGCThread(&bcIter->second, true));
    }
    
    //reassign ifTrue ifFalse Symbols
    symbolIfTrue  = symbolsMap["ifTrue:"];
    symbolIfFalse = symbolsMap["ifFalse:"];

/*
    map<string, GCSymbol*>::iterator it = symbolsMap.find("true");
    //pVMSymbol trueSym = (pVMSymbol) ReadBarrierForGCThread(&it->second);
    pVMSymbol trueSym = Untag(it->second);
    
    
    GCAbstractObject* raw_glob = globals[(GCSymbol*) trueSym];  // Cast is Performance HACK to avoid barrier!!!
    if (raw_glob == nullptr)
        raw_glob = globals[(GCSymbol*) Flip(trueSym)];
    
    //pVMObject glob_ptr_val = ReadBarrierForGCThread(&raw_glob);
    pVMObject glob_ptr_val = Untag(raw_glob);

    assert(glob_ptr_val == Untag(trueObject));
*/
    
    pthread_mutex_unlock(&testMutex);
    
    
}
void  Universe::CheckMarkingGlobals(void (*walk)(AbstractVMObject*)) {
    walk(Untag(nilObject));
    walk(Untag(trueObject));
    walk(Untag(falseObject));

    walk(Untag(objectClass));
    walk(Untag(classClass));
    walk(Untag(metaClassClass));
    
    walk(Untag(nilClass));
    walk(Untag(integerClass));
    walk(Untag(bigIntegerClass));
    walk(Untag(arrayClass));
    walk(Untag(methodClass));
    walk(Untag(symbolClass));
    walk(Untag(primitiveClass));
    walk(Untag(stringClass));
    walk(Untag(systemClass));
    walk(Untag(blockClass));
    walk(Untag(doubleClass));
    
    walk(Untag(threadClass));
    walk(Untag(mutexClass));
    walk(Untag(signalClass));
    
    walk(Untag(trueClass));
    walk(Untag(falseClass));
    
    // walk all entries in globals map
    map<GCSymbol*, GCAbstractObject*>::iterator iter;
    for (iter = globals.begin(); iter != globals.end(); iter++) {
        if (iter->second == NULL)
            continue;
        walk(Untag(iter->first));
        walk(Untag(iter->second));
    }
    
    // walk all entries in symbols map
    map<StdString, GCSymbol*>::iterator symbolIter;
    for (symbolIter = symbolsMap.begin();
         symbolIter != symbolsMap.end();
         symbolIter++) {
        //insert overwrites old entries inside the internal map
        walk(Untag(symbolIter->second));
    }
    
    map<long, GCClass*>::iterator bcIter;
    for (bcIter = blockClassesByNoOfArgs.begin();
         bcIter != blockClassesByNoOfArgs.end();
         bcIter++) {
        walk(Untag(bcIter->second));
    }
}
#else
void Universe::WalkGlobals(VMOBJECT_PTR (*walk)(VMOBJECT_PTR)) {
    nilObject   = (GCObject*) walk(READBARRIER(nilObject));
    trueObject  = (GCObject*) walk(READBARRIER(trueObject));
    falseObject = (GCObject*) walk(READBARRIER(falseObject));

#ifdef USE_TAGGING
    GlobalBox::updateIntegerBox(static_cast<VMInteger*>(walk(GlobalBox::IntegerBox())));
#endif

    objectClass    = (GCClass*) (walk(READBARRIER(objectClass)));
    classClass     = (GCClass*) (walk(READBARRIER(classClass)));
    metaClassClass = (GCClass*) (walk(READBARRIER(metaClassClass)));

    nilClass        = (GCClass*) (walk(READBARRIER(nilClass)));
    integerClass    = (GCClass*) (walk(READBARRIER(integerClass)));
    bigIntegerClass = (GCClass*) (walk(READBARRIER(bigIntegerClass)));
    arrayClass      = (GCClass*) (walk(READBARRIER(arrayClass)));
    methodClass     = (GCClass*) (walk(READBARRIER(methodClass)));
    symbolClass     = (GCClass*) (walk(READBARRIER(symbolClass)));
    primitiveClass  = (GCClass*) (walk(READBARRIER(primitiveClass)));
    stringClass     = (GCClass*) (walk(READBARRIER(stringClass)));
    systemClass     = (GCClass*) (walk(READBARRIER(systemClass)));
    blockClass      = (GCClass*) (walk(READBARRIER(blockClass)));
    doubleClass     = (GCClass*) (walk(READBARRIER(doubleClass)));
    
    threadClass     = (GCClass*) (walk(READBARRIER(threadClass)));
    mutexClass      = (GCClass*) (walk(READBARRIER(mutexClass)));
    signalClass     = (GCClass*) (walk(READBARRIER(signalClass)));
    
    trueClass  = (GCClass*) (walk(READBARRIER(trueClass)));
    falseClass = (GCClass*) (walk(READBARRIER(falseClass)));

#ifdef CACHE_INTEGER
    for (unsigned long i = 0; i < (INT_CACHE_MAX_VALUE - INT_CACHE_MIN_VALUE); i++)
#ifdef USE_TAGGING
        prebuildInts[i] = TAG_INTEGER(INT_CACHE_MIN_VALUE + i);
#else
        prebuildInts[i] = static_cast<pVMInteger>(walk(prebuildInts[i]));
#endif
#endif

    // walk all entries in globals map
    map<GCSymbol*, GCAbstractObject*> globs = globals;
    globals.clear();
    map<GCSymbol*, GCAbstractObject*>::iterator iter;
    for (iter = globs.begin(); iter != globs.end(); iter++) {
        if (iter->second == NULL)
            continue;

        GCSymbol* key = (GCSymbol*) (walk(READBARRIER(iter->first)));
        GCObject* val = (GCObject*) walk((VMOBJECT_PTR)iter->second);
        globals[key] = val;
    }
    
    // walk all entries in symbols map
    map<StdString, GCSymbol*>::iterator symbolIter;
    for (symbolIter = symbolsMap.begin();
         symbolIter != symbolsMap.end();
         symbolIter++) {
        //insert overwrites old entries inside the internal map
        symbolIter->second = (GCSymbol*) (walk(READBARRIER(symbolIter->second)));
    }

    map<long, GCClass*>::iterator bcIter;
    for (bcIter = blockClassesByNoOfArgs.begin();
         bcIter != blockClassesByNoOfArgs.end();
         bcIter++) {
        bcIter->second = (GCClass*) (walk(READBARRIER(bcIter->second)));
    }

    //reassign ifTrue ifFalse Symbols
    symbolIfTrue  = symbolsMap["ifTrue:"];
    symbolIfFalse = symbolsMap["ifFalse:"];
    
}
#endif

pVMMethod Universe::NewMethod( pVMSymbol signature,
        size_t numberOfBytecodes, size_t numberOfConstants) const {
    //Method needs space for the bytecodes and the pointers to the constants
    long additionalBytes = PADDED_SIZE(numberOfBytecodes + numberOfConstants*sizeof(pVMObject));

#if GC_TYPE==GENERATIONAL
    pVMMethod result = new (_HEAP, _PAGE, additionalBytes)
#elif GC_TYPE==PAUSELESS
    pVMMethod result = new (_HEAP, _UNIVERSE->GetInterpreter(), additionalBytes, true)
#else
    pVMMethod result = new (_HEAP,additionalBytes)
#endif
    VMMethod(numberOfBytecodes, numberOfConstants);

    result->SetClass(READBARRIER(methodClass));

    result->SetSignature(signature);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMMethod", result->GetObjectSize());
#endif

    return result;
}

pVMMutex Universe::NewMutex() const {
#if GC_TYPE==GENERATIONAL
    pVMMutex result = new (_HEAP, _PAGE) VMMutex();
#elif GC_TYPE==PAUSELESS
    pVMMutex result = new (_HEAP, _UNIVERSE->GetInterpreter()) VMMutex();
#else
    pVMMutex result = new (_HEAP) VMMutex();
#endif
    result->SetClass(READBARRIER(mutexClass));
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMMutex", sizeof(VMMutex));
#endif
    return result;
}

pVMSignal Universe::NewSignal() const {
#if GC_TYPE==GENERATIONAL
    pVMSignal result = new (_HEAP, _PAGE) VMSignal();
#elif GC_TYPE==PAUSELESS
    pVMSignal result = new (_HEAP, _UNIVERSE->GetInterpreter()) VMSignal();
#else
    pVMSignal result = new (_HEAP) VMSignal();
#endif
    result->SetClass(READBARRIER(signalClass));
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMSignal", sizeof(VMSignal));
#endif
    return result;
}

pVMThread Universe::NewThread() const {
#if GC_TYPE==GENERATIONAL
    pVMThread result = new (_HEAP, _PAGE) VMThread();
#elif GC_TYPE==PAUSELESS
    pVMThread result = new (_HEAP, _UNIVERSE->GetInterpreter()) VMThread();
#else
    pVMThread result = new (_HEAP) VMThread();
#endif
    //result->SetThreadId(threadCounter);
    //threadCounter += 1;
    result->SetClass(READBARRIER(threadClass));
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMThread", sizeof(VMThread));
#endif
    return result;
}

pVMString Universe::NewString( const StdString& str) const {
    return NewString(str.c_str());
}

pVMString Universe::NewString( const char* str) const {
#if GC_TYPE==GENERATIONAL
    pVMString result = new (_HEAP, _PAGE, PADDED_SIZE(strlen(str) + 1)) VMString(str);
#elif GC_TYPE==PAUSELESS
    pVMString result = new (_HEAP, _UNIVERSE->GetInterpreter(), PADDED_SIZE(strlen(str) + 1)) VMString(str);
#else
    pVMString result = new (_HEAP, PADDED_SIZE(strlen(str) + 1)) VMString(str);
#endif
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMString", result->GetObjectSize());
#endif
    return result;
}

pVMSymbol Universe::NewSymbol( const StdString& str) {
    return NewSymbol(str.c_str());
}

pVMSymbol Universe::NewSymbol( const char* str ) {
#if GC_TYPE==GENERATIONAL
    pVMSymbol result = new (_HEAP, _PAGE, PADDED_SIZE(strlen(str)+1)) VMSymbol(str);
#elif GC_TYPE==PAUSELESS
    pVMSymbol result = new (_HEAP, _UNIVERSE->GetInterpreter(), PADDED_SIZE(strlen(str)+1), true) VMSymbol(str);
#else
    pVMSymbol result = new (_HEAP, PADDED_SIZE(strlen(str)+1)) VMSymbol(str);
#endif
    symbolsMap[str] = WRITEBARRIER(result);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMSymbol", result->GetObjectSize());
#endif
    return result;
}

pVMClass Universe::NewSystemClass() const {
#if GC_TYPE==GENERATIONAL
    pVMClass systemClass = new (_HEAP, _PAGE) VMClass();
    systemClass->SetClass(new (_HEAP, _PAGE) VMClass());
#elif GC_TYPE==PAUSELESS
    pVMClass systemClass = new (_HEAP, _UNIVERSE->GetInterpreter(), 0, true) VMClass();
    systemClass->SetClass(new (_HEAP, _UNIVERSE->GetInterpreter(), 0, true) VMClass());
#else
    pVMClass systemClass = new (_HEAP) VMClass();
    systemClass->SetClass(new (_HEAP) VMClass());
#endif
    
    pVMClass mclass = systemClass->GetClass();

    mclass->SetClass(READBARRIER(metaClassClass));
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMClass", systemClass->GetObjectSize());
#endif

    return systemClass;
}

pVMSymbol Universe::SymbolFor(const StdString& str) {
    map<string, GCSymbol*>::iterator it = symbolsMap.find(str);
    
    if (it == symbolsMap.end()) {
        //sync_out(ostringstream() << "Create new symbol: " << str.c_str());
        return NewSymbol(str);
    } else {
        return READBARRIER(it->second);
    }
    //return (it == symbolsMap.end()) ? NewSymbol(str) : it->second;
}

pVMSymbol Universe::SymbolForChars(const char* str) {
    return SymbolFor(str);
}

void Universe::SetGlobal(pVMSymbol name, pVMObject val) {
    pthread_mutex_lock(&testMutex);
    globals[WRITEBARRIER(name)] = WRITEBARRIER(val);
    pthread_mutex_unlock(&testMutex);
}

void Universe::RemoveInterpreter() {
    pthread_mutex_lock(&interpreterMutex);
    interpreters.erase(std::remove(interpreters.begin(), interpreters.end(), this->GetInterpreter()), interpreters.end());
    pthread_mutex_unlock(&interpreterMutex);
}

#if GC_TYPE!=PAUSELESS
Interpreter* Universe::NewInterpreter() {
    Interpreter* interpreter = new Interpreter();
    pthread_setspecific(this->interpreterKey, interpreter);
    pthread_mutex_lock(&interpreterMutex);
    interpreters.push_back(interpreter);
    pthread_mutex_unlock(&interpreterMutex);
    return interpreter;
}

vector<Interpreter*>* Universe::GetInterpreters() {
    return &interpreters;
}
#else
Interpreter* Universe::NewInterpreter() {
    pthread_mutex_lock(_HEAP->GetNewInterpreterMutex());
    Interpreter* interpreter;
    pthread_mutex_lock(&interpreterMutex);
    if (interpreters.empty())
        interpreter = new Interpreter(false, true);
    else
        interpreter = new Interpreter(interpreters.back()->GetExpectedNMT(), interpreters.back()->GCTrapEnabled());
    pthread_setspecific(this->interpreterKey, interpreter);
    interpreters.push_back(interpreter);
    pthread_mutex_unlock(&interpreterMutex);
    pthread_mutex_unlock(_HEAP->GetNewInterpreterMutex());
    return interpreter;
}

unique_ptr<vector<Interpreter*>> Universe::GetInterpretersCopy() {
    pthread_mutex_lock(&interpreterMutex);
    unique_ptr<vector<Interpreter*>> copy(new vector<Interpreter*>(interpreters.begin(),interpreters.end()));
    pthread_mutex_unlock(&interpreterMutex);
    return copy;
}
#endif

// FOR DEBUGGING PURPOSES
/*
void Universe::PrintGlobals() {
    map<GCSymbol*, GCAbstractObject*>::iterator it;
    for (it = globals.begin(); it != globals.end(); it++) {
        sync_out(ostringstream() << "[GLOBALS] symbol: " << Untag(it->first)->GetChars()
                 << " ptr: " << it->first << " value ptr: " << it->second);
    }
} */


