// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SC2_CPU_AFFINITY_H
#define SC2_CPU_AFFINITY_H

#include <algorithm>
#include <vector>

namespace sc2 {

struct CpuFreqInfo {
    bool online = true;
    long info_max_khz = 0;
    long scaling_max_khz = 0;
    bool isolated = false;
};

inline long effectiveCoreFreqKhz(const CpuFreqInfo &core)
{
    if (!core.online || core.isolated) return 0;
    if (core.info_max_khz > 0 && core.scaling_max_khz > 0) {
        return std::min(core.info_max_khz, core.scaling_max_khz);
    }
    return core.info_max_khz > 0 ? core.info_max_khz : core.scaling_max_khz;
}

// Selects active CPU cores for the main game + inline presenter thread:
// 1) Excludes offline and thermally isolated cores (effectiveCoreFreqKhz == 0).
// 2) On heterogeneous big.LITTLE / DynamIQ SoCs (max_info_max > min_info_max), cores with
//    info_max_khz > min_info_max are out-of-order performance cores (Big / Mid / Prime), while
//    cores at min_info_max are in-order LITTLE efficiency cores (e.g. Cortex-A53/A55).
// 3) Whenever >= 2 performance cores are online, restricts selection strictly to performance
//    cores so Android EAS never migrates the single-threaded main loop onto slow LITTLE cores,
//    and never evicts 2-big-core clusters (2+6 SoCs) into LITTLE-only masks when scaling_max_freq
//    thermally throttles.
// 4) On 3-tier SoCs (e.g. 1x Prime + 3x Mid + 4x LITTLE):
//    - When avoid_prime_thermal is false and Prime leads Mid by >= 15% (e.g. 2.84 vs 2.42 GHz,
//      or 2.02 vs 1.40 GHz), pins the Prime core(s) so EAS doesn't drop the main thread onto a
//      slower Mid core while cool.
//    - When avoid_prime_thermal is true (hot Prime die sensor) and >= 2 non-Prime performance
//      cores remain online, shifts the main loop onto the Mid performance cluster so the Prime core
//      can cool down before firmware thermal daemons isolate it and clamp the Mid cluster.
inline std::vector<int> SelectActiveCores(const std::vector<CpuFreqInfo> &cores,
    bool avoid_prime_thermal = false)
{
    const size_t n = cores.size();
    std::vector<long> effective(n, 0);
    long best_all = 0;
    int available = 0;
    long min_info_max = 0;
    long max_info_max = 0;

    for (size_t i = 0; i < n; ++i) {
        effective[i] = effectiveCoreFreqKhz(cores[i]);
        if (effective[i] > 0) {
            ++available;
            if (effective[i] > best_all) best_all = effective[i];
        }
        const long hw_max = cores[i].info_max_khz > 0 ? cores[i].info_max_khz : effective[i];
        if (hw_max > 0) {
            if (min_info_max == 0 || hw_max < min_info_max) min_info_max = hw_max;
            if (hw_max > max_info_max) max_info_max = hw_max;
        }
    }

    std::vector<int> selected;
    if (best_all <= 0 || available == 0) return selected;

    int perf_online = 0;
    int non_prime_perf_online = 0;
    long best_perf = 0;
    long best_little = 0;
    if (max_info_max > min_info_max) {
        for (size_t i = 0; i < n; ++i) {
            const long hw_max = cores[i].info_max_khz > 0 ? cores[i].info_max_khz : effective[i];
            if (effective[i] > 0) {
                if (hw_max > min_info_max) {
                    ++perf_online;
                    if (hw_max < max_info_max) ++non_prime_perf_online;
                    if (effective[i] > best_perf) best_perf = effective[i];
                } else {
                    if (effective[i] > best_little) best_little = effective[i];
                }
            }
        }
    }

    const bool exclude_prime = avoid_prime_thermal && (non_prime_perf_online >= 2);
    if (exclude_prime) {
        perf_online = 0;
        best_perf = 0;
        best_all = 0;
        available = 0;
        for (size_t i = 0; i < n; ++i) {
            const long hw_max = cores[i].info_max_khz > 0 ? cores[i].info_max_khz : effective[i];
            if (hw_max >= max_info_max) effective[i] = 0;
            if (effective[i] > 0) {
                ++available;
                if (effective[i] > best_all) best_all = effective[i];
                if (hw_max > min_info_max) {
                    ++perf_online;
                    if (effective[i] > best_perf) best_perf = effective[i];
                }
            }
        }
        if (best_all <= 0 || available == 0) return selected;
    }

    const bool use_perf_only = (perf_online >= 2 && best_perf > 0);
    const long reference_best = use_perf_only ? best_perf : best_all;

    long second_tier_perf = 0;
    if (use_perf_only) {
        for (size_t i = 0; i < n; ++i) {
            const long hw_max = cores[i].info_max_khz > 0 ? cores[i].info_max_khz : effective[i];
            if (effective[i] > 0 && hw_max > min_info_max && effective[i] < best_perf) {
                if (effective[i] > second_tier_perf) second_tier_perf = effective[i];
            }
        }
    }

    const bool pin_prime_only = (!exclude_prime && use_perf_only && second_tier_perf > 0 &&
        best_perf * 100 >= second_tier_perf * 115);

    auto isCandidate = [&](size_t i) -> bool {
        if (effective[i] <= 0) return false;
        if (!use_perf_only) return true;
        const long hw_max = cores[i].info_max_khz > 0 ? cores[i].info_max_khz : effective[i];
        return hw_max > min_info_max;
    };

    std::vector<bool> picked(n, false);
    int picked_count = 0;
    if (pin_prime_only) {
        for (size_t i = 0; i < n; ++i) {
            if (isCandidate(i) && effective[i] * 100 >= best_perf * 98) {
                picked[i] = true;
                ++picked_count;
            }
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (isCandidate(i) && effective[i] * 100 >= reference_best * 70) {
                picked[i] = true;
                ++picked_count;
            }
        }
    }

    const int min_cores = use_perf_only ? std::min(2, perf_online) : std::min(4, available);
    while (!pin_prime_only && picked_count < min_cores) {
        long next_best = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!picked[i] && isCandidate(i) && effective[i] > next_best) next_best = effective[i];
        }
        if (next_best <= 0) break;
        for (size_t i = 0; i < n; ++i) {
            if (!picked[i] && isCandidate(i) && effective[i] == next_best) {
                picked[i] = true;
                ++picked_count;
            }
        }
    }

    // On 2+6 SoCs (perf_online == 2), if the 2 BIG cores are severely thermally throttled below
    // 80% of the LITTLE cluster's effective clock (e.g. 1.21 GHz vs 1.80 GHz LITTLE), keep both
    // BIG cores in the mask (never evict them) and add up to 2 highest-index LITTLE cores (4 cores total)
    // so thermal pressure on the BIG cluster can subside and clock speed can recover.
    if (use_perf_only && perf_online == 2 && best_little > 0 && best_perf * 100 < best_little * 80) {
        int added_little = 0;
        for (size_t idx = n; idx > 0 && added_little < 2; --idx) {
            const size_t i = idx - 1;
            if (!picked[i] && effective[i] > 0 && effective[i] == best_little) {
                picked[i] = true;
                ++picked_count;
                ++added_little;
            }
        }
    }

    selected.reserve(static_cast<size_t>(picked_count));
    for (size_t i = 0; i < n; ++i) {
        if (picked[i]) selected.push_back(static_cast<int>(i));
    }
    return selected;
}

} // namespace sc2

#endif
