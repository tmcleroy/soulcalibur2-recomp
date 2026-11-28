// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SC2_ANDROID_TELEMETRY_H
#define SC2_ANDROID_TELEMETRY_H

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace sc2 {

struct ClusterFreqSample {
    int cpu = -1;
    long max_khz = 0;
    long cur_khz = 0;
};

struct ThermalZoneSample {
    std::string type;
    double temp_c = 0.0;
    bool valid = false;
};

inline bool ParseThermalTempCelsius(long raw_temp, double &out_celsius)
{
    if (raw_temp <= 0) return false;
    double celsius = 0.0;
    if (raw_temp > 1000) {
        celsius = static_cast<double>(raw_temp) / 1000.0;
    } else if (raw_temp > 150) {
        celsius = static_cast<double>(raw_temp) / 10.0;
    } else {
        celsius = static_cast<double>(raw_temp);
    }
    if (celsius < 5.0 || celsius > 125.0) return false;
    out_celsius = celsius;
    return true;
}

inline int ThermalZonePriority(const std::string &type)
{
    std::string lower;
    lower.reserve(type.size());
    for (unsigned char ch : type) {
        lower.push_back(static_cast<char>(std::tolower(ch)));
    }
    auto has = [&](const char *needle) {
        return lower.find(needle) != std::string::npos;
    };
    if (has("cpu") || has("soc") || has("tsens") || has("mtktscpu") ||
        has("exynos") || has("qcom") || has("core") || has("tboard") ||
        has("ap_therm") || has("ap-therm")) {
        return 3;
    }
    if (has("batt") || has("charger") || has("skin") || has("usb") || has("wlc")) {
        return 1;
    }
    return type.empty() ? 1 : 2;
}

inline bool SelectBestThermalZone(const std::vector<ThermalZoneSample> &zones, ThermalZoneSample &out)
{
    int best_priority = 0;
    bool found = false;
    for (const auto &zone : zones) {
        if (!zone.valid) continue;
        const int prio = ThermalZonePriority(zone.type);
        if (!found || prio > best_priority || (prio == best_priority && zone.temp_c > out.temp_c)) {
            best_priority = prio;
            out = zone;
            found = true;
        }
    }
    return found;
}

inline std::string FormatClusterCaps(const std::vector<ClusterFreqSample> &samples)
{
    std::string out;
    for (const auto &s : samples) {
        if (s.cpu < 0 || s.max_khz <= 0) continue;
        char one[32];
        std::snprintf(one, sizeof one, "%scpu%d %.2f", out.empty() ? "" : ", ", s.cpu, s.max_khz / 1e6);
        out += one;
    }
    return out.empty() ? std::string("unknown") : (out + " GHz");
}

inline std::string FormatClusterCur(const std::vector<ClusterFreqSample> &samples)
{
    std::string out;
    for (const auto &s : samples) {
        if (s.cpu < 0 || s.cur_khz <= 0) continue;
        char one[32];
        std::snprintf(one, sizeof one, "%scpu%d %.2f", out.empty() ? "" : ", ", s.cpu, s.cur_khz / 1e6);
        out += one;
    }
    return out.empty() ? std::string("n/a") : (out + " GHz");
}

} // namespace sc2

#endif
