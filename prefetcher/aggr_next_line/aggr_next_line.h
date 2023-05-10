#ifndef AGGR_NEXT_H
#define AGGR_NEXT_H

#define AGGRESSIVENESS_MAX      16

#include "champsim_constants.h"

#include <cstdint>
#include <deque>
#include <map>

class MISRA_TABLE {
    private:
        std::multimap<uint64_t, uint64_t> table;
        int size;
    public:
        // Constructor
        MISRA_TABLE(int sz):size(sz) {}
        
        // Function to update table
        bool update_table(std::int64_t addr) {
            // Increment count of existing element or add new element to table
            auto it = table.find(addr);
            if (it != table.end()) {
                it->second++;
            }
            else if (table.size() < size) {
                table.insert(std::make_pair(addr, 1));
            }
            else {
                // Decrement count of all elements and remove those whose count reaches 0
                for (auto it = table.begin(); it != table.end(); ) {
                    it->second--;
                    if (it->second == 0) {
                        it = table.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }
            return true;
        }
};

#endif