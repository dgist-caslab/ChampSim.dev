#include "aggr_next_line.h"
#include "cache.h"

extern CACHE LLC;

void CACHE::prefetcher_initialize() { std::cout << NAME << " aggressive next-line prefetcher for LLC" << std::endl; }

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in)
{
  // [PHW] cache static
  // uint64_t pf_requested = 0, pf_issued = 0, pf_useful = 0, pf_useless = 0, pf_fill = 0;
  uint64_t pf_addr = addr + (1 << LOG2_BLOCK_SIZE);
  // uint64_t llc_pf_accuracy = LLC.roi_stats.pf_useful ? ((100 * LLC.roi_stats.pf_useful) / LLC.roi_stats.pf_issued) : 0;
  // [PHW] TODO adjust aggresiveness by using pf accuracy. refer to SPP.
  int aggresiveness = metadata_in ? ((AGGRESSIVENESS_MAX * metadata_in) / 100) : 1;
  // int aggresiveness = AGGRESSIVENESS_MAX;
  if(aggresiveness >= AGGRESSIVENESS_MAX){
    aggresiveness = AGGRESSIVENESS_MAX;
  }
  // cout << "[PHW]llc_pf_useful: " << LLC.pf_useful << "\tllc_pf_issued: " << LLC.pf_issued << endl;
  // std::cout << "DEBUG metadata_in: " << metadata_in << std::endl;
  // std::cout << "DEBUG aggresiveness: " << aggresiveness << std::endl;
  int pf_cnt = 0;
  while (1) {
    if ((addr & ~(PAGE_SIZE - 1)) == (pf_addr & ~(PAGE_SIZE - 1))) { // Prefetch request is in the same physical page
      if(pf_cnt >= aggresiveness){
        break;
      }
      // int ret = prefetch_line(ip, addr, pf_addr, true, 0);
      int ret = prefetch_line(pf_addr, true, 0);
      if(ret == 0){
        break;
      }
      pf_addr = pf_addr + (1 << LOG2_BLOCK_SIZE);
      pf_cnt++;
    } else {
      break;
    }
  }
  // std::cout << "[PHW]llc_pf_accuracy: " << metadata_in << "\taggresiveness: " << aggresiveness << "\tprefetched: " << pf_cnt << "\taddr:0x" << addr << std::endl;
  return 0;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate() {}

void CACHE::prefetcher_final_stats() {}