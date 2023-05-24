#include "page_lru.h"

uint64_t PAGE_LRU::get_size(){
    return lru_size;
}

void PAGE_LRU::insert_ppage(uint64_t pfn, bool _pf, bool _ref, bool toHead){
//   if(pfn == 0xb3c000){
//     std::cout << "0xb3c000 inserted" << std::endl;
//   }
  page_t new_page;
  new_page.pfn = pfn;
  new_page.bit_pf = _pf;
  new_page.bit_ref = _ref;

  std::unique_lock<std::mutex> lock(mtx_);
  if (toHead) {
    page_list.push_front(new_page);
    auto it = page_list.begin();
    pages[pfn] = it;
  } else {
    pages[pfn] = page_list.insert(page_list.end(), new_page);
  }
  lru_size++;
  lock.unlock();
}

bool PAGE_LRU::find_and_update(uint64_t pfn){
    if (pages.find(pfn) != pages.end()) {
        auto it = pages[pfn];
        it->bit_ref = true;
        // std::cout << "pfn: " << pfn << "\tfound" << std::endl;
        std::unique_lock<std::mutex> lock(mtx_);
        page_list.splice(page_list.begin(), page_list, it);
        pages[pfn] = page_list.begin();
        lock.unlock();
        return true;
    } else {
        return false;
    }
}
std::tuple<uint64_t, bool, bool> PAGE_LRU::evict_mru(){
    if(lru_size > 0){
        auto ret = std::make_tuple(page_list.front().pfn, page_list.front().bit_pf, page_list.front().bit_ref);
        std::unique_lock<std::mutex> lock(mtx_);
        pages.erase(page_list.front().pfn);
        page_list.pop_front();
        lock.unlock();
        lru_size--;
        return ret;
    }
    return std::make_tuple(0, false, false);
}

std::tuple<uint64_t, bool, bool> PAGE_LRU::evict_lru(){
    //[PHW] must check bit_ref
    if(lru_size > 0){
        while(page_list.back().bit_ref){ //[PHW] skip page with bit_ref
            auto it = std::prev(page_list.end()); // get last element's iterator
            std::unique_lock<std::mutex> lock(mtx_);
            page_list.back().bit_ref = false;
            page_list.splice(page_list.begin(), page_list, it); // relocate to front
            pages[page_list.front().pfn] = page_list.begin();
            lock.unlock();
        }

        // evict page
        auto ret = std::make_tuple(page_list.back().pfn, page_list.back().bit_pf, page_list.back().bit_ref);
        std::unique_lock<std::mutex> lock(mtx_);
        // std::cout << "debug evict1: " << std::hex << page_list.back().pfn << std::endl;
        pages.erase(page_list.back().pfn);
        // std::cout << "debug evict2: " << std::hex << page_list.back().pfn << std::endl;
        page_list.pop_back();
        // std::cout << "debug evict3: " << std::hex << page_list.back().pfn << std::endl;
        lru_size--;
        lock.unlock();
        return ret;
    }
    return std::make_tuple(0, false, false);
}

bool PAGE_MGMT::demote(){
    // 0. get new page from CXL. use vmem interface
    uint64_t slow_new_ppage = vmem->ppage_slow_front();
    // 1. get inacitve last element
    // std::cout << "debug demote" << std::endl;
    auto ret = lru_list[1].evict_lru(); // get inactive_back, which is non referenced page
    bool flag_found = false;
    if (std::get<0>(ret) > 0) {
        for (auto& entry : vmem->vpage_to_ppage_map) {
            // entry.first.first    -> cpu
            // entry.first.second   -> vfn
            // entry.second         -> ppage
            if (entry.second == std::get<0>(ret)) {
              // 2. return demotion target ppage(fast) to ppage_free_list_fast
              vmem->ppage_free_list_fast.push_back(entry.second);
              std::cout << "[DEMOTION]cpu: " << std::hex << entry.first.first << "\tva: " << std::hex << entry.first.second << "\torigin_pa: " << std::hex
                        << entry.second << "\tnew_pa: " << std::hex << slow_new_ppage << std::endl;
            //   std::cout << "#_of_fast_page_remain: " << std::dec << vmem->ppage_free_list_fast.size() << " / " << std::dec << watermark_low
                        // << "\t#_of_slow_page_remain: " << vmem->ppage_free_list_slow.size() << "\t#_p: " << num_promotion << "\t#_d: " << num_demotion
                        // << std::endl;
              entry.second = slow_new_ppage;
            //   std::cout << "new ppage in slow: " << std::hex << entry.second << std::endl;
              flag_found = true;
              break;
            }
        }
        // if(!flag_found){
            // for (auto& entry : vmem->vpage_to_ppage_map) {
                // std::cout<<"[DEMOTION-ERROR]ppage_target: " << std::hex << std::get<0>(ret) << "\t" << entry.second << std::endl;
            // }
        // }
        assert(flag_found);
        // insert new slow page to slow inactive list
        lru_list[3].insert_ppage(slow_new_ppage, false, false, true);
        return true;
    } else {
        // eviction from inactive tail is failed, give the ppage back to slow-memory
        vmem->ppage_free_list_slow.push_front(slow_new_ppage);
        return false;
    }
}

bool PAGE_MGMT::promote(){
    // [PHW]
    bool flag_found = false;
    uint64_t new_fast_ppage = 0;
    uint64_t target_ppage;
    bool target_pf, target_ref;
    // 0. get ppage from fast memory
    if(vmem->ppage_free_list_fast.size() > 0){
        new_fast_ppage = vmem->ppage_front();
    }else{
        return false;
    }
    // 1. get head of slow active list
    if(lru_list[2].get_size() > 0){
        auto ret = lru_list[2].evict_mru();
        if(std::get<0>(ret) > 0){
            target_ppage = std::get<0>(ret);
            target_pf = std::get<1>(ret);
            target_ref = std::get<2>(ret);
        }else{
            vmem->ppage_free_list_fast.push_back(new_fast_ppage);
            return false;
        }
    }else{
        return false;
    }
    // 2. find and update entry of vapge_to_ppage_map according to 1.
    for(auto& entry : vmem->vpage_to_ppage_map){
        if (entry.second == target_ppage) {
            std::cout << "[PROMOTION]cpu: " << entry.first.first << "\tva: " << entry.first.second << "\torigin_pa: " << entry.second
                      << "\tnew_pa: " << new_fast_ppage << std::endl;
            // std::cout << "#_of_fast_page_remain: " << std::dec << vmem->ppage_free_list_fast.size() << " / " << std::dec << watermark_low
            //           << "\t#_of_slow_page_remain: " << vmem->ppage_free_list_slow.size() << "\t#_p: " << num_promotion << "\t#_d: " << num_demotion
            //           << std::endl;
            entry.second = new_fast_ppage; // update va-to-pa mapping
            // std::cout << "insert into fa in promotion" << std::endl;
            lru_list[0].insert_ppage(new_fast_ppage, target_pf, target_ref, true);
            flag_found = true;
            vmem->ppage_free_list_slow.push_back(target_ppage);
            break;
        }
    }
    if(flag_found){
        return true;
    }else{
        return flag_found;
    }
}

void PAGE_MGMT::page_reference(uint64_t address){
    bool isSlow = (address > 4294967296) ? true : false; // hardcoded for 4G fast mem. if isSlow true, this is a NUMA hint fault?
    uint64_t pfn = address & (0xFFFFFFFF << 12); // delete offset
    // std::cout << "ref_pa: " << address << std::endl;
    // std::cout << "pfn: " << pfn << std::endl;
    // check address is new one
    bool pfn_is_new = true;
    if(!isSlow){
        pfn_is_new = !(lru_list[0].find_and_update(pfn) || lru_list[1].find_and_update(pfn));
        // for(int i = 0;i<2;i++){
        //     pfn_is_new = !lru_list[i].find_and_update(pfn);
            // std::cout << "found at: " << i << std::endl;
        // }
    }else{
        pfn_is_new = !(lru_list[0].find_and_update(pfn) || lru_list[1].find_and_update(pfn));
        // for(int i = 2;i<4;i++){
        //     pfn_is_new = !lru_list[i].find_and_update(pfn);
            // std::cout << "found at: " << i << std::endl;
        // }
    }

    if(pfn_is_new){
        if(!isSlow){ // insert into fast-active
            // std::cout << "insert into fa for new" << std::endl;
            lru_list[0].insert_ppage(pfn, false, true, true);
        }else{ // insert into slow-active
            // std::cout << "insert into fi for new" << std::endl;
            lru_list[2].insert_ppage(pfn, false, true, true);
        }
    }
    if(vmem->ppage_free_list_fast.size() % 100 == 0)
        std::cout << "#_of_fast_page_remain: " << std::dec << vmem->ppage_free_list_fast.size() << " / " << std::dec << watermark_low << "\t#_of_slow_page_remain: " << vmem->ppage_free_list_slow.size() << "\t#_p: " << num_promotion << "\t#_d: " << num_demotion << std::endl;

    adjust_list_rate(); // it maintain the ratio of active/inactive page list

    if(vmem->ppage_free_list_fast.size() <= watermark_low){ // start demotion when fast-memory's idle page touched at low watermark
        demotion_flag = true;
        promotion_flag = false;
    }
    if (demotion_flag && !fast_mem->migrate && !slow_mem->migrate) {
        // if(last_migrate_cycle == 0 || (_cycle - last_migrate_cycle) > MIGRATE_WINDOW_CYCLE){
            if(demote()){
                // last_migrate_cycle = _cycle;
                fast_mem->migrate = true;
                slow_mem->migrate = true;
                fast_mem->leap_operation += MIGRATE_WINDOW_CYCLE;
                slow_mem->leap_operation += MIGRATE_WINDOW_CYCLE;
                num_demotion++;
            }
        // }
    }
    // [PHW] promotion 
    if(!demotion_flag && promotion_flag && !fast_mem->migrate && !slow_mem->migrate){
        // if(last_migrate_cycle == 0 || (_cycle - last_migrate_cycle) > MIGRATE_WINDOW_CYCLE){
            if(promote()){
                // last_migrate_cycle = _cycle;
                fast_mem->migrate = true;
                slow_mem->migrate = true;
                fast_mem->leap_operation += MIGRATE_WINDOW_CYCLE;
                slow_mem->leap_operation += MIGRATE_WINDOW_CYCLE;
                num_promotion++;
            }
        // }
    }

    if(vmem->ppage_free_list_fast.size() >= watermark_high){ // stop demotion
        demotion_flag = false;
        promotion_flag = true;
    }
}
bool PAGE_MGMT::adjust_list_rate(){
    bool adjusted = false;
    bool fast_adjusted = false;
    bool slow_adjusted = false;
    uint64_t fa = lru_list[0].get_size();
    uint64_t fi = lru_list[1].get_size();
    uint64_t sa = lru_list[2].get_size();
    uint64_t si = lru_list[3].get_size();

    // about fast mem
    // std::cout << "fa / fi : " << std::dec << fa << " / " << fi << std::endl;
    if(fi > 0){
        // std::cout << "fa / fi : " << fa << " / " << fi << " = " << fa / fi << std::endl;
        if( (fa / fi) > FAST_TARGET_RATE){
            // de-activate
            // std::cout << "debug adjust0" << std::endl;
            auto ret = lru_list[0].evict_lru(); // get active_back, which is non referenced lru page
            if(std::get<0>(ret) > 0){
                // std::cout << "insert into fa" << std::endl;
                lru_list[1].insert_ppage((uint64_t)std::get<0>(ret), (bool)std::get<1>(ret), (bool)std::get<2>(ret), true);// insert to inactive
                // std::cout << "fast de-activated" << std::endl;
                adjusted = true;
                fast_adjusted = true;
            }
        }else if( (fa / fi) < FAST_TARGET_RATE){
            // activate
            // std::cout << "debug adjust1" << std::endl;
            auto ret = lru_list[1].evict_mru(); // get inactive_front
            if(std::get<0>(ret) > 0){
                // std::cout << "insert into fi" << std::endl;
                lru_list[0].insert_ppage((uint64_t)std::get<0>(ret), (bool)std::get<1>(ret), (bool)std::get<2>(ret), true);// insert to active
                // std::cout << "fast activated" << std::endl;
                adjusted = true;
                fast_adjusted = true;
            }
        }
    }
    fa = lru_list[0].get_size();
    fi = lru_list[1].get_size();
    if(fi == 0 && fa > 1 && !fast_adjusted){ // first contact with fast inactive
        // de-activate
        // std::cout << "si_first" << std::endl;
        auto ret = lru_list[0].evict_lru(); // get active_back, which is non referenced lru page
        if(std::get<0>(ret) > 0){
                // std::cout << "insert into fi" << std::endl;
            lru_list[1].insert_ppage((uint64_t)std::get<0>(ret), (bool)std::get<1>(ret), (bool)std::get<2>(ret), true);// insert to inactive
            adjusted = true;
        }
        fi_first = true;
    }
    // about slow mem
    if(si > 0){
        if( (sa / si) > SLOW_TARGET_RATE){
            // de-activate
            auto ret = lru_list[2].evict_lru(); // get active_back, which is non referenced page
            if(std::get<0>(ret) > 0){
                // std::cout << "insert into sa" << std::endl;
                lru_list[3].insert_ppage((uint64_t)std::get<0>(ret), (bool)std::get<1>(ret), (bool)std::get<2>(ret), true);// insert to inactive
                // std::cout << "slow de-activated" << std::endl;
                adjusted = true;
                slow_adjusted = true;
            }
        }else if( (sa / si) < SLOW_TARGET_RATE){
            // activate
            auto ret = lru_list[3].evict_mru(); // get inactive_front
            if(std::get<0>(ret) > 0){
                // std::cout << "insert into si" << std::endl;
                lru_list[2].insert_ppage((uint64_t)std::get<0>(ret), (bool)std::get<1>(ret), (bool)std::get<2>(ret), true);// insert to active
                // std::cout << "slow activated" << std::endl;
                adjusted = true;
                slow_adjusted = true;
            }
        }
    }
    if(si == 0 && sa > 1 && !slow_adjusted){ // first contact with slow inactive
        // de-activate
        auto ret = lru_list[2].evict_lru(); // get active_back, which is non referenced page
        if(std::get<0>(ret) > 0){
                // std::cout << "insert into si" << std::endl;
            lru_list[3].insert_ppage((uint64_t)std::get<0>(ret), (bool)std::get<1>(ret), (bool)std::get<2>(ret), true);// insert to inactive
            adjusted = true;
        }
        si_first = true;
    }
    return adjusted;
}