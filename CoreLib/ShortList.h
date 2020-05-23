#ifndef CORELIB_SHORT_LIST_H
#define CORELIB_SHORT_LIST_H

#include "List.h"

namespace CoreLib
{
    namespace Basic
    {
        template<typename T, int size=16>
        class ShortList
        {
        private:
            List<T> overflow;
            T shortList[size] = {};
            int count = 0;
        public:
            void Add(T obj)
            {
                if (count < size)
                {
                    shortList[count] = _Move(obj);
                }
                else
                {
                    overflow.Add(_Move(obj));
                }
                count++;
            }

            void SetSize(int newSize)
            {
                count = newSize;
                if (newSize > size)
                    overflow.SetSize(newSize - size);
                else
                    overflow.Clear();
            }

            int Count()
            {
                return count;
            }

            T& Last()
            {
                if (count < size) return shortList[count - 1];
                return overflow[count - size - 1];
            }

            T& operator[](int index)
            {
                if (index < size)
                    return shortList[index];
                else
                    return overflow[index - size];
            }

            void Clear()
            {
                count = 0;
                overflow.Clear();
            }

            void Free()
            {
                overflow = decltype(overflow)();
            }
        };
    }
}

#endif