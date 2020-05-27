#include "stdafx.h"
#include "CppUnitTest.h"
#include "CoreLib/Basic.h"
#include "CoreLib/VariableSizeAllocator.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace CoreLib;

namespace UnitTest
{
    TEST_CLASS(VariableSizeAllocatorTest)
    {
    public:
        TEST_METHOD(VSAllocPattern1)
        {
            VariableSizeAllocator allocator;
            allocator.InitPool(12);
            auto ptr0 = allocator.Alloc(4);
            Assert::IsTrue(ptr0 == 0);
            auto ptr1 = allocator.Alloc(4);
            Assert::IsTrue(ptr1 == 4);
            auto ptr2 = allocator.Alloc(4);
            Assert::IsTrue(ptr2 == 8);
            Assert::IsTrue(allocator.Alloc(1) == -1);
            allocator.Free(ptr0, 4);
            allocator.Free(ptr2, 4);
            allocator.Free(ptr1, 4);
            auto ptr3 = allocator.Alloc(9);
            Assert::IsTrue(ptr3 == 0);
            allocator.Free(ptr3, 9);
        }
        TEST_METHOD(VSAllocPattern2)
        {
            VariableSizeAllocator allocator;
            allocator.InitPool(12);
            auto ptr0 = allocator.Alloc(1);
            auto ptr1 = allocator.Alloc(2);
            auto ptr2 = allocator.Alloc(3);
            auto ptr3 = allocator.Alloc(4);
            allocator.Free(ptr1, 2);
            allocator.Free(ptr0, 1);
            auto ptr4 = allocator.Alloc(3);
            Assert::IsTrue(ptr4 == 0);
        }
        TEST_METHOD(VSAllocPattern3)
        {
            VariableSizeAllocator allocator;
            allocator.InitPool(12);
            auto ptr0 = allocator.Alloc(1);
            auto ptr1 = allocator.Alloc(2);
            auto ptr2 = allocator.Alloc(3);
            auto ptr3 = allocator.Alloc(4);
            allocator.Free(ptr3, 4);
            allocator.Free(ptr2, 3);
            allocator.Free(ptr1, 2);
            allocator.Free(ptr0, 1);
            auto ptr4 = allocator.Alloc(10);
            Assert::IsTrue(ptr4 == 0);
        }
        TEST_METHOD(VSAllocPattern4)
        {
            for (int t = 0; t < 20; t++)
            {
                struct Allocation { int offset, size; };
                CoreLib::Basic::List<bool> states;
                CoreLib::Basic::List<Allocation> allocations;
                states.SetSize(271 + t * 31);
                for (auto& s : states) s = false;
                VariableSizeAllocator allocator;
                allocator.InitPool(states.Count());
                srand(12317 * t);
                for (int i = 0; i < 1000; i++)
                {
                    int op = rand() % 3;
                    if (op == 0 && allocations.Count() > 0)
                    {
                        // free
                        int id = rand() % allocations.Count();
                        allocator.Free(allocations[id].offset, allocations[id].size);
                        for (int j = allocations[id].offset; j < allocations[id].offset + allocations[id].size; j++)
                            states[j] = false;
                        allocations.RemoveAt(id);
                        continue;
                    }
                    // allocate
                    int size = rand() % (t + 13) + 1;
                    int offset = allocator.Alloc(size);
                    if (offset == -1)
                    {
                        // verify that we are indeed full
                        int ptr = 0;
                        int freeBlocks = 0;
                        while (ptr < states.Count())
                        {
                            if (!states[ptr])
                            {
                                freeBlocks++;
                                if (freeBlocks == size)
                                    Assert::Fail(L"free blocks found but allocation failed.");
                            }
                            else
                            {
                                freeBlocks = 0;
                            }
                            ptr++;
                        }
                    }
                    else
                    {
                        // verify the blocks are unoccupied
                        for (int j = offset; j < offset + size; j++)
                        {
                            Assert::IsTrue(!states[j], L"Allocated space is already occupied.");
                            states[j] = true;
                        }
                        allocations.Add(Allocation{ offset, size });
                    }
                }
            }
        }
    };
}