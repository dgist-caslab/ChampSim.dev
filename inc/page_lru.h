#ifndef PAGE_LRU_H
#define PAGE_LRU_H

#include "champsim_constants.h"
#include "vmem.h"
#include "dram_controller.h"

#include <stdint.h>
#include <list>
#include <map>
#include <queue>
#include <iostream>
#include <mutex>
#include <tuple>

#define FAST_TARGET_RATE        3
#define SLOW_TARGET_RATE        10
#define MIGRATE_WINDOW_CYCLE    10800
// #define MIGRATE_WINDOW_CYCLE    0


enum list_idx{
    FAST_ACTIVE,
    FAST_INACTIVE,
    SLOW_ACTIVE,
    SLOW_INACTIVE,
    FIRST_APPEARED
};

class PAGE_LRU
{
public:
  PAGE_LRU(){
    lru_size = 0;
  }
  struct page_t {
    uint64_t pfn;
    bool bit_ref;
    bool bit_pf;
  };

  bool find_and_update(uint64_t pfn);
  void insert_ppage(uint64_t pfn, bool _pf, bool _ref, bool toHead);
  std::tuple<uint64_t, bool, bool> evict_lru();
  std::tuple<uint64_t, bool, bool> evict_mru();
  uint64_t get_size();

private:
  uint64_t lru_size;
  std::map<uint64_t, std::list<page_t>::iterator> pages;
  std::list<page_t> page_list;
  std::mutex mtx_;
};

class PAGE_MGMT
{
    public:
        PAGE_MGMT(MEMORY_CONTROLLER* _fast_mem, MEMORY_CONTROLLER* _slow_mem, VirtualMemory* m_vmem, uint64_t low, uint64_t high) :
            fast_mem(_fast_mem), slow_mem(_slow_mem), vmem(m_vmem),watermark_low((vmem->fast_mem_page_num * low)/100), watermark_high((vmem->fast_mem_page_num * high)/100){
            // fast_mem(_fast_mem), slow_mem(_slow_mem), cpu{cpu0, cpu1, cpu2, cpu3}, vmem(m_vmem),watermark_low(vmem->fast_mem_page_num - (vmem->fast_mem_page_num * low)/100), watermark_high(vmem->fast_mem_page_num - (vmem->fast_mem_page_num * high)/100){
            std::cout << "page lru initiated" << std::endl;
            // for(int i = 0 ;i<4;i++){
            //     std::cout << "idx:" << i << "\tsize: " << lru_list[i].list_size << std::endl;
            // }
            std::cout << "watermark_low: " << watermark_low << " / " << low << std::endl;
            std::cout << "watermark_high: " << watermark_high << " / " << high << std::endl;
            last_migrate_cycle = 0;
            demotion_flag = false;
            promotion_flag = false;
            num_promotion = 0;
            num_demotion = 0;
            fi_first = false;
            si_first = false;
        }
        bool adjust_list_rate();
        void page_reference(uint64_t address);
        // bool update_lru_lists(uint64_t address);
        // bool check_fast_idle_capacity();
        // void first_appeared(uint64_t address, bool isSlow);
        // // bool cascade_update();
    private:
        MEMORY_CONTROLLER* fast_mem;
        MEMORY_CONTROLLER* slow_mem;
        VirtualMemory* vmem;
        uint64_t fast_mem_size;
        uint64_t watermark_low;
        uint64_t watermark_high;
        PAGE_LRU lru_list[4];
        uint64_t last_migrate_cycle;
        bool demotion_flag, promotion_flag;
        uint64_t num_promotion, num_demotion;
        bool fi_first;
        bool si_first;
        bool demote();
        bool promote();
};

#endif