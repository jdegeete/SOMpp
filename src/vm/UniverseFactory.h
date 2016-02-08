# pragma once

class Universe;

class UniverseFactory {
    
public:
    
    UniverseFactory();
    UniverseFactory(Universe*);
    
    VMArray* NewArray(long) const;
    VMArray* NewArrayList(ExtendedList<vm_oop_t>& list) const;
    VMArray* NewArrayList(ExtendedList<VMInvokable*>& list) const;
    VMArray* NewArrayList(ExtendedList<VMSymbol*>& list) const;
    VMArray* NewArrayFromStrings(const vector<StdString>&) const;
    VMBlock* NewBlock(VMMethod*, VMFrame*, long);
    VMClass* NewClass(VMClass*) const;
    VMFrame* NewFrame(VMFrame*, VMMethod*) const;
    VMMethod* NewMethod(VMSymbol*, size_t, size_t) const;
    VMObject* NewInstance(VMClass*) const;
    VMInteger* NewInteger(int64_t) const;
    VMDouble* NewDouble(double) const;
    VMClass* NewMetaclassClass(void) const;
    VMString* NewString(const StdString&) const;
    VMSymbol* NewSymbol(const StdString&);
    VMString* NewString(const char*) const;
    VMSymbol* NewSymbol(const char*);
    VMClass* NewSystemClass(void) const;
    
private:
    Universe* universe;
    
};

