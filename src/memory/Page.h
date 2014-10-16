//
//  Page.h
//  SOM
//
//  Created by Jeroen De Geeter on 6/04/14.
//
//

#ifndef SOM_Page_h
#define SOM_Page_h

#include <cstdlib>
#include "PagedHeap.h"

using namespace std;

class Page {
    
public:
    
    Page(void* pageStart, PagedHeap* heap);
    
    AbstractVMObject* AllocateObject(size_t size);
    void ClearPage();
    
#if GC_TYPE==PAUSELESS
    void Block();
    void UnBlock();
    bool Blocked();
    pVMObject LookupNewAddress(VMOBJECT_PTR);
    void AddAmountLiveData(size_t);
    
    //void GetPageStart();
    //void ClearMarkBits();
#endif
    
private:
    size_t pageStart;
    size_t pageEnd;
    void* collectionLimit;
    void* volatile nextFreePosition;
    PagedHeap* heap;
    
#if GC_TYPE==PAUSELESS
    bool blocked;
    pVMObject* sideArray;
    long amountLiveData;
    
    //bool used;
    //vector<AbstractVMObject*> forwardReferences;
    
#endif
    
};

#endif