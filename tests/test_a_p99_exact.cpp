// Independent nearest-rank oracle for Track A's p99 max fast path.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
using u32 = std::uint32_t;
std::array<u32, 3> candidate(std::vector<u32> a) {
    unsigned n = a.size();
    if (n <= 2) {
        u32 low = std::min(a[0], a[n - 1]), high = std::max(a[0], a[n - 1]);
        return {low, high, high};
    }
    if (n <= 32) std::sort(a.begin(), a.end());
    auto start = a.begin();
    std::array<u32, 3> result{};
    constexpr unsigned ranks[] = {50, 95, 99};
    for (unsigned j = 0; j < 3; ++j) {
        auto rank = a.begin() + (ranks[j] * n + 99) / 100 - 1;
        if (n > 32) {
            if (j == 2 && n < 100) {
                result[j] = *std::max_element(start, a.end());
                break;
            }
            std::nth_element(start, rank, a.end());
        }
        result[j] = *rank;
        start = rank;
    }
    return result;
}
std::array<u32, 3> reference(std::vector<u32> a) {
    std::sort(a.begin(), a.end());
    unsigned n = a.size();
    return {a[(50*n+99)/100-1], a[(95*n+99)/100-1], a[(99*n+99)/100-1]};
}
int main() {
    std::mt19937_64 rng(20261031);
    unsigned tested = 0;
    for (unsigned n = 1; n <= 500; ++n) for (int k = 0; k < 64; ++k) {
        std::vector<u32> values(n);
        for (auto& x : values) {
            if (k % 6 == 0) x = 0;
            else if (k % 6 == 1) x = UINT32_MAX;
            else if (k % 6 == 2) x = rng() % 2;
            else if (k % 6 == 3) x = rng() % 100;
            else x = static_cast<u32>(rng());
        }
        if (k % 7 == 0) std::sort(values.begin(), values.end());
        if (k % 7 == 1) std::sort(values.rbegin(), values.rend());
        if (candidate(values) != reference(values)) {
            std::printf("FAIL n=%u k=%d\n", n, k); return 1;
        }
        ++tested;
    }
    for (unsigned n : {1000u, 4097u, 9223u}) for (int k = 0; k < 16; ++k) {
        std::vector<u32> values(n);
        for (auto& x : values) x = static_cast<u32>(rng());
        if (candidate(values) != reference(values)) {
            std::puts("FAIL large"); return 1;
        }
        ++tested;
    }
    std::printf("PASS %u nearest-rank cases\n", tested);
}
