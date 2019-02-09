#ifndef GAME_ENGINE_DISJOINT_SET_H
#define GAME_ENGINE_DISJOINT_SET_H

#include "CoreLib/Basic.h"

namespace GameEngine
{
    struct DisjointSet
    {
    private:
        struct Entry
        {
            int Parent;
            int Rank;
        };
        CoreLib::List<Entry> entries;
    public:
        void Init(int n)
        {
            entries.SetSize(n);
            for (int i = 0; i < n; i++)
            {
                entries[i].Parent = i;
                entries[i].Rank = 0;
            }
        }
        int Find(int x)
        {
            while (entries[x].Parent != x)
            {
                int next = entries[x].Parent;
                entries[x].Parent = entries[next].Parent;
                entries[x].Rank = entries[entries[x].Parent].Rank + 1;
                x = next;
            }
            return x;
        }
        int Union(int x, int y)
        {
            int px = Find(x);
            int py = Find(y);
            if (px != py)
            {
                if (entries[px].Rank < entries[py].Rank)
                {
                    int t = px; px = py; py = t;
                }
                entries[py].Parent = px;
                if (entries[px].Rank == entries[py].Rank)
                {
                    entries[px].Rank++;
                }
            }
            return px;
        }
    };
}
#endif