#ifndef CORELIB_VARIABLE_SIZE_ALLOCATOR_H
#define CORELIB_VARIABLE_SIZE_ALLOCATOR_H

#include "Common.h"

namespace CoreLib
{
    class VariableSizeAllocator
    {
    public:
        struct FreeListNode
        {
            int Offset;
            int Length;
            FreeListNode* prev;
            FreeListNode* next;
        };
        FreeListNode* freeListHead = nullptr;
    public:
        ~VariableSizeAllocator()
        {
            auto list = freeListHead;
            while (list)
            {
                auto next = list->next;
                delete list;
                list = next;
            }
        }
        void InitPool(int numElements)
        {
            freeListHead = new FreeListNode();
            freeListHead->prev = freeListHead->next = nullptr;
            freeListHead->Offset = 0;
            freeListHead->Length = numElements;
        }

        int Alloc(int size)
        {
            if (!freeListHead) return -1;
            auto freeBlock = freeListHead;
            while (freeBlock && freeBlock->Length < size)
                freeBlock = freeBlock->next;
            if (!freeBlock || freeBlock->Length < size)
                return -1;
            int result = freeBlock->Offset;
            freeBlock->Offset += size;
            freeBlock->Length -= size;
            if (freeBlock->Length == 0)
            {
                if (freeBlock->prev) freeBlock->prev->next = freeBlock->next;
                if (freeBlock->next) freeBlock->next->prev = freeBlock->prev;
                if (freeBlock == freeListHead)
                    freeListHead = freeBlock->next;
                delete freeBlock;
            }
            return result;
        }
        void Free(int offset, int size)
        {
            if (!freeListHead)
            {
                freeListHead = new FreeListNode();
                freeListHead->next = freeListHead->prev = nullptr;
                freeListHead->Length = size;
                freeListHead->Offset = offset;
                return;
            }
            auto freeListNode = freeListHead;
            FreeListNode* prevFreeNode = nullptr;
            while (freeListNode && freeListNode->Offset < offset + size)
            {
                prevFreeNode = freeListNode;
                freeListNode = freeListNode->next;
            }
            FreeListNode* newNode = new FreeListNode();
            newNode->Offset = offset;
            newNode->Length = size;
            newNode->prev = prevFreeNode;
            newNode->next = freeListNode;
            if (freeListNode) freeListNode->prev = newNode;
            if (prevFreeNode) prevFreeNode->next = newNode;
            if (freeListNode == freeListHead)
                freeListHead = newNode;
            if (prevFreeNode && prevFreeNode->Offset + prevFreeNode->Length == newNode->Offset)
            {
                prevFreeNode->Length += newNode->Length;
                prevFreeNode->next = freeListNode;
                if (freeListNode) freeListNode->prev = prevFreeNode;
                delete newNode;
                newNode = prevFreeNode;
            }
            if (freeListNode && newNode->Offset + newNode->Length == freeListNode->Offset)
            {
                newNode->Length += freeListNode->Length;
                newNode->next = freeListNode->next;
                if (freeListNode->next) freeListNode->next->prev = newNode;
                delete freeListNode;
            }
        }
    };
}

#endif