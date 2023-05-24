/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "vmem.h"

#include <cassert>

#include "champsim.h"
#include "champsim_constants.h"
#include "dram_controller.h"
#include "util.h"

VirtualMemory::VirtualMemory(uint64_t page_table_page_size, std::size_t page_table_levels, uint64_t minor_penalty, MEMORY_CONTROLLER& dram, MEMORY_CONTROLLER& smem)
    : next_ppage(VMEM_RESERVE_CAPACITY), last_ppage(1ull << (LOG2_PAGE_SIZE + champsim::lg2(page_table_page_size / PTE_BYTES) * page_table_levels)),
      minor_fault_penalty(minor_penalty), pt_levels(page_table_levels), pte_page_size(page_table_page_size), ppage_free_list_fast(((uint64_t)(dram.size() - VMEM_RESERVE_CAPACITY) / PAGE_SIZE), PAGE_SIZE),
      ppage_free_list_slow(((uint64_t)smem.size_slow() / PAGE_SIZE), PAGE_SIZE)
{
  assert(page_table_page_size > 1024);
  assert(page_table_page_size == (1ull << champsim::lg2(page_table_page_size)));
  assert(last_ppage > VMEM_RESERVE_CAPACITY);

  //[PHW] restore old fashion
  // populate fast and slow free list
  ppage_free_list_fast.front() = VMEM_RESERVE_CAPACITY;
  std::partial_sum(std::cbegin(ppage_free_list_fast), std::cend(ppage_free_list_fast), std::begin(ppage_free_list_fast));
  // for(const auto& elem : ppage_free_list_fast){
  //   std::cout << "popul_ppage: " << std::hex << elem << std::endl;
  // }
  ppage_free_list_slow.front() = ppage_free_list_fast.back() + PAGE_SIZE;
  std::partial_sum(std::cbegin(ppage_free_list_slow), std::cend(ppage_free_list_slow), std::begin(ppage_free_list_slow));
  fast_mem_page_num = ppage_free_list_fast.size();
  std::cout << "[PHW]debug1: " << ppage_free_list_fast.front() << " ~ " << ppage_free_list_fast.back() << "\tsize(): " << fast_mem_page_num << std::endl;

  last_fast_ppage = dram.size();
  std::cout << last_fast_ppage << std::endl; 
  slow_mem_page_num = ppage_free_list_slow.size();
  std::cout << "[PHW]debug2: " << ppage_free_list_slow.front() << " ~ " << ppage_free_list_slow.back() << "\tsize(): " << slow_mem_page_num << std::endl;
  // [PHW] do i need shuffle?
  // std::shuffle(std::begin(ppage_free_list_fast), std::end(ppage_free_list_fast), std::mt19937_64{200});
  // std::shuffle(std::begin(ppage_free_list_slow), std::end(ppage_free_list_slow), std::mt19937_64{200});

  auto required_bits = champsim::lg2(last_ppage);
  std::cout << "physical memory(" << dram.size() << " + " << smem.size_slow() << ")\tvirtual memory size(" << last_ppage << ")" << std::endl;
  std::cout << "fast-mem range: " << VMEM_RESERVE_CAPACITY << " ~ " << last_fast_ppage << " ~ " << last_fast_ppage + smem.size_slow() << std::endl;
  if (required_bits > 64)
    std::cout << "WARNING: virtual memory configuration would require " << required_bits << " bits of addressing." << std::endl;
  if (required_bits > champsim::lg2(dram.size() + smem.size_slow())){
    std::cout << "WARNING: physical memory(" << champsim::lg2(dram.size() + smem.size_slow()) << ") size is smaller than virtual memory size(" << required_bits << ")" << std::endl;
  }
  next_ppage = ppage_free_list_fast.front(); // after this, ppage_front() will return ppage_free_list_fast.front()
  ppage_free_list_fast.pop_front();
}

uint64_t VirtualMemory::shamt(std::size_t level) const { return LOG2_PAGE_SIZE + champsim::lg2(pte_page_size / PTE_BYTES) * (level - 1); }

uint64_t VirtualMemory::get_offset(uint64_t vaddr, std::size_t level) const
{
  return (vaddr >> shamt(level)) & champsim::bitmask(champsim::lg2(pte_page_size / PTE_BYTES));
}

uint64_t VirtualMemory::ppage_front() const
{
  assert(available_ppages() > 0);
  // std::cout << "next_ppage: " << next_ppage << std::endl;
  return next_ppage;
}
uint64_t VirtualMemory::ppage_slow_front()
{
  uint64_t slow_ppage = ppage_free_list_slow.front();
  assert(slow_ppage > 0);
  ppage_free_list_slow.pop_front();
  return slow_ppage;
}

void VirtualMemory::ppage_pop() { 
  //[PHW] allocate fast mem first
  if(ppage_free_list_fast.size() > 0){
    next_ppage = ppage_free_list_fast.front();
    ppage_free_list_fast.pop_front();
  }else{
    next_ppage = ppage_free_list_slow.front();
    ppage_free_list_slow.pop_front();
  }
  // if(ppage_reclaimed_list_fast.size() > 0){
  // next_ppage += PAGE_SIZE;
}

std::size_t VirtualMemory::available_ppages() const { return (last_ppage - next_ppage) / PAGE_SIZE; }

std::pair<uint64_t, uint64_t> VirtualMemory::va_to_pa(uint32_t cpu_num, uint64_t vaddr)
{
  auto [ppage, fault] = vpage_to_ppage_map.insert({{cpu_num, vaddr >> LOG2_PAGE_SIZE}, ppage_front()});

  // this vpage doesn't yet have a ppage mapping
  if (fault){
    // std::cout << "[new]cpu: " << cpu_num << "\tva: " << std::hex << vaddr << "\tva_log: " << std::hex << (vaddr >> LOG2_PAGE_SIZE) << "\tpa: " << std::hex << ppage->second << std::endl;
    ppage_pop();
  }else{
    // std::cout << "[already]cpu: " << cpu_num << "\tva: " << std::hex << vaddr << "\tva_log: " << std::hex << (vaddr >> LOG2_PAGE_SIZE) << "\tpa: " << std::hex << ppage->second << std::endl;
  }
  return {champsim::splice_bits(ppage->second, vaddr, LOG2_PAGE_SIZE), fault ? minor_fault_penalty : 0};
}

std::pair<uint64_t, uint64_t> VirtualMemory::get_pte_pa(uint32_t cpu_num, uint64_t vaddr, std::size_t level)
{
  if (next_pte_page == 0) {
    next_pte_page = ppage_front();
    ppage_pop();
  }

  std::tuple key{cpu_num, vaddr >> shamt(level), level};
  auto [ppage, fault] = page_table.insert({key, next_pte_page});

  // this PTE doesn't yet have a mapping
  if (fault) {
    next_pte_page = ppage_front();
    ppage_pop();
    // next_pte_page += pte_page_size;
    // if (!(next_pte_page % PAGE_SIZE)) { //[PHW] pte page size always 4096, so it is not required
    //   next_pte_page = ppage_front();
    //   ppage_pop();
    // }
  }

  auto offset = get_offset(vaddr, level);
  auto paddr = champsim::splice_bits(ppage->second, offset * PTE_BYTES, champsim::lg2(pte_page_size));
  if constexpr (champsim::debug_print) {
    std::cout << "[VMEM] " << __func__;
    std::cout << " paddr: " << std::hex << paddr;
    std::cout << " vaddr: " << vaddr << std::dec;
    std::cout << " pt_page offset: " << offset;
    std::cout << " translation_level: " << level;
    if (fault)
      std::cout << " PAGE FAULT";
    std::cout << std::endl;
  }

  return {paddr, fault ? minor_fault_penalty : 0};
}
