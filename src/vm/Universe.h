#pragma once

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

//#define __DEBUG
#include <map>
#include <vector>

#include "../misc/defs.h"
#include "../misc/Timer.h"
#include "../misc/ExtendedList.h"

#include "../vmobjects/ObjectFormats.h"

#include "../interpreter/Interpreter.h"

#include "../memory/Heap.h"

#include "UniverseFactory.h"

class SourcecodeCompiler;

// for runtime debug
extern short dumpBytecodes;
extern short gcVerbosity;

//global VMObjects
extern GCObject* nilObject;
extern GCObject* trueObject;
extern GCObject* falseObject;

extern GCClass* objectClass;
extern GCClass* classClass;
extern GCClass* metaClassClass;

extern GCClass* nilClass;
extern GCClass* integerClass;
extern GCClass* arrayClass;
extern GCClass* methodClass;
extern GCClass* symbolClass;
extern GCClass* primitiveClass;
extern GCClass* stringClass;
extern GCClass* systemClass;
extern GCClass* blockClass;
extern GCClass* doubleClass;

extern GCClass* trueClass;
extern GCClass* falseClass;

extern GCSymbol* symbolIfTrue;
extern GCSymbol* symbolIfFalse;

using namespace std;
class Universe {
    
    friend class UniverseFactory;
    
public:
    inline Universe* operator->();

    //static methods
    static void Start(long argc, char** argv);
    static void Quit(long);
    static void ErrorExit(const char*);

    Interpreter* GetInterpreter() {
        return interpreter;
    }
    
    void Assert(bool) const;

    VMSymbol* SymbolFor(const StdString&);
    VMSymbol* SymbolForChars(const char*);

    //VMObject instanciation methods. These methods are all inlined
    VMArray* NewArray(long size) const { return factory.NewArray(size); };
    VMArray* NewArrayList(ExtendedList<vm_oop_t>& list) const { return factory.NewArrayList(list); };
    VMArray* NewArrayList(ExtendedList<VMInvokable*>& list) const { return factory.NewArrayList(list); };
    VMArray* NewArrayList(ExtendedList<VMSymbol*>& list) const { return factory.NewArrayList(list); };
    VMArray* NewArrayFromStrings(const vector<StdString>& strings) const { return factory.NewArrayFromStrings(strings); };
    VMBlock* NewBlock(VMMethod* method, VMFrame* context, long arguments) { return factory.NewBlock(method, context, arguments); };
    VMClass* NewClass(VMClass* classOfClass) const { return factory.NewClass(classOfClass); };
    VMFrame* NewFrame(VMFrame* previousFrame, VMMethod* method) const { return factory.NewFrame(previousFrame, method); };
    VMMethod* NewMethod(VMSymbol* signature, size_t numberOfBytecodes, size_t numberOfConstants) const { return factory.NewMethod(signature, numberOfBytecodes, numberOfConstants); };
    VMObject* NewInstance(VMClass* classOfInstance) const { return factory.NewInstance(classOfInstance); };
    VMInteger* NewInteger(int64_t value) const { return factory.NewInteger(value); };
    VMDouble* NewDouble(double value) const { return factory.NewDouble(value); };
    VMClass* NewMetaclassClass() const { return factory.NewMetaclassClass(); };
    VMString* NewString(const StdString& str) const { return factory.NewString(str); };
    VMSymbol* NewSymbol(const StdString& str) { return factory.NewSymbol(str); };
    VMString* NewString(const char* str) const { return factory.NewString(str); };
    VMSymbol* NewSymbol(const char* str) { return factory.NewSymbol(str); };
    VMClass* NewSystemClass() const { return factory.NewSystemClass(); };
    
    void WalkGlobals(walk_heap_fn);

    void InitializeSystemClass(VMClass*, VMClass*, const char*);

    vm_oop_t GetGlobal(VMSymbol*);
    void SetGlobal(VMSymbol* name, vm_oop_t val);
    bool HasGlobal(VMSymbol*);
    void InitializeGlobals();
    VMClass* GetBlockClass(void) const;
    VMClass* GetBlockClassWithArgs(long);

    VMClass* LoadClass(VMSymbol*);
    void LoadSystemClass(VMClass*);
    VMClass* LoadClassBasic(VMSymbol*, VMClass*);
    VMClass* LoadShellClass(StdString&);

    Universe();
    ~Universe();
#ifdef LOG_RECEIVER_TYPES
    struct stat_data {
        long noCalls;
        long noPrimitiveCalls;
    };
    map<StdString, long> receiverTypes;
    map<StdString, stat_data> callStats;
#endif
    //
    
    static bool IsValidObject(vm_oop_t obj);

private:
    UniverseFactory factory;
    
    vector<StdString> handleArguments(long argc, char** argv);
    long getClassPathExt(vector<StdString>& tokens, const StdString& arg) const;

    friend Universe* GetUniverse();
    static Universe* theUniverse;

    long setupClassPath(const StdString& cp);
    long addClassPath(const StdString& cp);
    void printUsageAndExit(char* executable) const;

    void initialize(long, char**);

    long heapSize;
    
    map<GCSymbol*, gc_oop_t> globals;
    map<long, GCClass*> blockClassesByNoOfArgs;
    vector<StdString> classPath;
    
    map<string, GCSymbol*> symbolsMap;


    Interpreter* interpreter;
};

//Singleton accessor
inline Universe* GetUniverse() __attribute__ ((always_inline));
Universe* GetUniverse() {
    if (DEBUG && !Universe::theUniverse) {
        Universe::ErrorExit("Trying to access uninitialized Universe, exiting.");
    }
    return Universe::theUniverse;
}

Universe* Universe::operator->() {
    if (DEBUG && !theUniverse) {
        ErrorExit("Trying to access uninitialized Universe, exiting.");
    }
    return theUniverse;
}
