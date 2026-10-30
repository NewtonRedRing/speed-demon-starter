// Reproducible synthetic kernel microbenchmark (NOT a competition board score).
// g++ -std=c++20 -O3 -march=native tests/benchmark_country_bitsets.cpp -o /tmp/bitbench
// /usr/bin/time -f '%M KB peak RSS' /tmp/bitbench sets
// /usr/bin/time -f '%M KB peak RSS' /tmp/bitbench candidate
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

int main(int argc, char** argv) {
 const bool candidate = argc>=2 && argv[1][0]=='c';
 const int n=3320000, countries=240, pool=100000;
 std::mt19937 rng(9876543);
 std::vector<std::string> names;
 names.reserve(pool);
 for(int k=0;k<pool;++k) names.push_back("name_"+std::to_string(k));
 std::vector<std::pair<int,uint32_t>> events;
 events.reserve(n);
 for(int i=0;i<n;++i) events.emplace_back(rng()%countries,rng()%pool);
 const auto start=std::chrono::steady_clock::now();
 uint64_t total=0;
 if(candidate) {
   std::unordered_map<std::string_view,uint32_t> dict;
   dict.reserve(130000);
   std::vector<std::vector<uint64_t>> masks(countries);
   for(const auto &[country,raw]:events) {
     auto [it,created]=dict.emplace(names[raw], static_cast<uint32_t>(dict.size()));
     uint32_t id=it->second;
     auto& vec=masks[country];
     size_t word=id/64;
     if(vec.size()<=word) vec.resize(word+1);
     uint64_t mask=1ULL<<(id%64);
     if(!(vec[word]&mask)) {vec[word]|=mask;++total;}
   }
 }else{
   std::vector<std::unordered_set<std::string_view>> sets(countries);
   for(const auto &[country,raw]:events) total+=sets[country].insert(names[raw]).second;
 }
 auto secs=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 std::cout << (candidate?"bitset":"sets")<<" distinct="<<total<<" seconds="<<secs<<"\n";
}
