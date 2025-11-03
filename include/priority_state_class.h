#ifndef PPTPN_INCLUDE_PRIORITY_STATE_CLASS_H
#define PPTPN_INCLUDE_PRIORITY_STATE_CLASS_H

#include "priority_time_petri_net.h"
#include <utility>
#include <vector>
#include <map>
#include <limits>
#include <algorithm>
#include <string>
#include <sstream>

using namespace ptpn;

namespace priority_scg
{
    // 时间区间模板类
    template <typename T = int>
    class TimeIntervalT
    {
    public:
        T lower; // 下界
        T upper; // 上界,如果是无穷大则使用类型的最大值表示

        // 构造函数
        explicit TimeIntervalT(T lower = T(0), T upper = std::numeric_limits<T>::max())
            : lower(lower), upper(upper) {}

        // TODO: 交集没有考虑全局时钟,尝试把等待时间设置为单数值
        TimeIntervalT intersect(const TimeIntervalT &other) const
        {
            return TimeIntervalT(std::max(lower, other.lower), std::min(upper, other.upper));
        }

        [[nodiscard]] bool is_valid() const { return lower <= upper && lower >= 0; }
        [[nodiscard]] bool is_empty() const { return !is_valid(); }
        [[nodiscard]] bool equals(const TimeIntervalT &other) const
        {
            return lower == other.lower && upper == other.upper;
        }


        bool contains(const T &value) const
        {
            return lower <= value && value <= upper;
        }

        TimeIntervalT shift(const T &offset) const
        {
            T new_lower = lower == std::numeric_limits<T>::min() && offset < 0 ? std::numeric_limits<T>::min() : lower + offset;

            T new_upper = upper == std::numeric_limits<T>::max() && offset > 0 ? std::numeric_limits<T>::max() : upper + offset;

            return TimeIntervalT(new_lower, new_upper);
        }

        bool operator==(const TimeIntervalT &other) const
        {
            return lower == other.lower && upper == other.upper;
        }

        bool operator!=(const TimeIntervalT &other) const
        {
            return !(*this == other);
        }

        [[nodiscard]] std::string to_string() const
        {
            std::string upper_str = upper == std::numeric_limits<T>::max() ? "∞" : std::to_string(upper);
            return "[" + std::to_string(lower) + ", " + upper_str + "]";
        }
    };

    using TimeInterval = TimeIntervalT<int>;

    using Marking = std::map<ptpn_v_desc, int>;

    template <typename T = int>
    class PriorityStateClassT
    {
    public:
        using interval_type = TimeIntervalT<T>;

        PriorityStateClassT() = default;
        PriorityStateClassT(
            Marking m,
            const std::map<ptpn_v_desc, interval_type> &enabled_rts,
            const std::map<ptpn_v_desc, interval_type> &suspended_rts)
            : marking(std::move(m)),
              enabled_runtimes(enabled_rts),
              suspended_runtimes(suspended_rts) {}

        PriorityStateClassT(
            Marking m,
            const std::map<ptpn_v_desc, std::pair<T, T>> &enabled_rts,
            const std::map<ptpn_v_desc, std::pair<T, T>> &suspended_rts)
            : marking(std::move(m))
        {
            for (const auto &[t, rt] : enabled_rts)
            {
                enabled_runtimes[t] = interval_type(rt.first, rt.second);
            }

            for (const auto &[t, rt] : suspended_rts)
            {
                suspended_runtimes[t] = interval_type(rt.first, rt.second);
            }
        }

        [[nodiscard]] const Marking &get_marking() const { return marking; }

        void set_token(const ptpn_v_desc place, const int tokens)
        {
            if (tokens > 0)
            {
                marking[place] = tokens;
            }
            else
            {
                marking.erase(place);
            }
        }

        [[nodiscard]] int get_token(const ptpn_v_desc place) const
        {
            auto it = marking.find(place);
            return it != marking.end() ? it->second : 0;
        }

        void add_tokens(const ptpn_v_desc place, const int tokens)
        {
            if (tokens != 0)
            {
                const int new_tokens = get_token(place) + tokens;
                set_token(place, new_tokens);
            }
        }

        bool remove_tokens(const ptpn_v_desc place, const int tokens)
        {
            if (tokens <= 0)
                return true;

            const int current = get_token(place);
            if (current < tokens)
                return false;

            set_token(place, current - tokens);
            return true;
        }

        [[nodiscard]] bool has_enough_tokens(const ptpn_v_desc place, const int required) const
        {
            return get_token(place) >= required;
        }

        const std::map<ptpn_v_desc, interval_type> &get_enabled_runtimes() const
        {
            return enabled_runtimes;
        }

        void set_enabled_runtime(ptpn_v_desc transition, const interval_type &interval)
        {
            if (interval.is_valid())
            {
                enabled_runtimes[transition] = interval;
                suspended_runtimes.erase(transition);
            }
            else
            {
                enabled_runtimes.erase(transition);
            }
        }

        interval_type get_enabled_runtime(ptpn_v_desc transition) const
        {
            auto it = enabled_runtimes.find(transition);
            return it != enabled_runtimes.end() ? it->second : interval_type();
        }

        [[nodiscard]] bool is_transition_enabled(ptpn_v_desc transition) const
        {
            return enabled_runtimes.find(transition) != enabled_runtimes.end();
        }

        [[nodiscard]] std::vector<ptpn_v_desc> get_enabled_transitions() const
        {
            std::vector<ptpn_v_desc> result;
            result.reserve(enabled_runtimes.size());

            for (const auto &[trans, _] : enabled_runtimes)
            {
                result.push_back(trans);
            }

            return result;
        }

        const std::map<ptpn_v_desc, interval_type> &get_suspended_runtimes() const
        {
            return suspended_runtimes;
        }

        void set_suspended_runtime(ptpn_v_desc transition, const interval_type &interval)
        {
            if (interval.is_valid())
            {
                suspended_runtimes[transition] = interval;
                enabled_runtimes.erase(transition);
            }
            else
            {
                suspended_runtimes.erase(transition);
            }
        }

        interval_type get_suspended_runtime(ptpn_v_desc transition) const
        {
            auto it = suspended_runtimes.find(transition);
            return it != suspended_runtimes.end() ? it->second : interval_type();
        }

        [[nodiscard]] bool is_transition_suspended(ptpn_v_desc transition) const
        {
            return suspended_runtimes.find(transition) != suspended_runtimes.end();
        }

        [[nodiscard]] std::vector<ptpn_v_desc> get_suspended_transitions() const
        {
            std::vector<ptpn_v_desc> result;
            result.reserve(suspended_runtimes.size());

            for (const auto &[trans, _] : suspended_runtimes)
            {
                result.push_back(trans);
            }

            return result;
        }

        void elapse_time(T time_units)
        {
            if (time_units <= 0)
                return;

            for (auto &[trans, interval] : enabled_runtimes)
            {
                interval = interval.shift(-time_units);
            }

            for (auto &[trans, interval] : suspended_runtimes)
            {
                interval = interval.shift(-time_units);
            }
        }

        [[nodiscard]] bool is_valid() const
        {
            for (const auto &[_, interval] : enabled_runtimes)
            {
                if (!interval.is_valid())
                    return false;
            }

            for (const auto &[_, interval] : suspended_runtimes)
            {
                if (!interval.is_valid())
                    return false;
            }

            for (const auto &[trans, _] : enabled_runtimes)
            {
                if (suspended_runtimes.find(trans) != suspended_runtimes.end())
                {
                    return false;
                }
            }

            return true;
        }

        bool operator==(const PriorityStateClassT &other) const;
        bool operator!=(const PriorityStateClassT &other) const
        {
            return !(*this == other);
        }

        [[nodiscard]] std::string to_string() const;
        [[nodiscard]] std::size_t hash() const;
        [[nodiscard]] std::string get_marking_string() const
        {
            std::stringstream ss;
            ss << "{";
            bool first = true;
            for (const auto &[place, tokens] : marking)
            {
                if (first)
                    first = false;
                else
                    ss << ", ";
                ss << "P" << place << ":" << tokens;
            }
            ss << "}";
            return ss.str();
        }

        void normalize()
        {
            for (auto it = marking.begin(); it != marking.end();) {
                if (it->second <= 0) {
                    it = marking.erase(it);
                } else {
                    ++it;
                }
            }

            for (auto it = enabled_runtimes.begin(); it != enabled_runtimes.end();) {
                it->second = it->second.normalize();
                if (!it->second.is_valid()) {
                    it = enabled_runtimes.erase(it);
                } else {
                    ++it;
                }
            }

            for (auto it = suspended_runtimes.begin(); it != suspended_runtimes.end();) {
                it->second = it->second.normalize();
                if (!it->second.is_valid()) {
                    it = suspended_runtimes.erase(it);
                } else {
                    ++it;
                }
            }
        }

    private:
        Marking marking;
        std::map<ptpn_v_desc, interval_type> enabled_runtimes;
        std::map<ptpn_v_desc, interval_type> suspended_runtimes;
    };

    using PriorityStateClass = PriorityStateClassT<>;

    template <typename T>
    bool PriorityStateClassT<T>::operator==(const PriorityStateClassT &other) const
    {
        if (marking.size() != other.marking.size()) {
            return false;
        }

        for (const auto &[place, tokens] : marking) {
            if (tokens <= 0) continue;

            auto it = other.marking.find(place);
            if (it == other.marking.end() || it->second != tokens) {
                return false;
            }
        }

        if (enabled_runtimes.size() != other.enabled_runtimes.size()) {
            return false;
        }

        for (const auto &[t, runtime] : enabled_runtimes) {
            auto it = other.enabled_runtimes.find(t);
            if (it == other.enabled_runtimes.end() || !runtime.equals(it->second)) {
                return false;
            }
        }

        if (suspended_runtimes.size() != other.suspended_runtimes.size()) {
            return false;
        }

        for (const auto &[t, runtime] : suspended_runtimes) {
            auto it = other.suspended_runtimes.find(t);
            if (it == other.suspended_runtimes.end() || !runtime.equals(it->second)) {
                return false;
            }
        }

        return true;
    }

    template <typename T>
    std::string PriorityStateClassT<T>::to_string() const
    {
        std::stringstream ss;

        ss << "Marking: {";
        bool first = true;
        for (const auto &[place, tokens] : marking)
        {
            if (tokens <= 0)
                continue;

            if (!first)
            {
                ss << ", ";
            }
            ss << "p" << place << ":" << tokens;
            first = false;
        }
        ss << "}\n";

        ss << "Enabled runtimes: {";
        first = true;
        for (const auto &[t, runtime] : enabled_runtimes)
        {
            if (!first)
            {
                ss << ", ";
            }
            ss << "t" << t << ":" << runtime.to_string();
            first = false;
        }
        ss << "}\n";

        ss << "Suspended runtimes: {";
        first = true;
        for (const auto &[t, runtime] : suspended_runtimes)
        {
            if (!first)
            {
                ss << ", ";
            }
            ss << "t" << t << ":" << runtime.to_string();
            first = false;
        }
        ss << "}";

        return ss.str();
    }

    template <typename T>
    std::size_t PriorityStateClassT<T>::hash() const
    {
        // 使用类似 boost::hash_combine 的方法，避免 XOR 导致的碰撞
        // hash_combine: seed ^= hash(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        std::size_t hash_value = 0;
        const std::size_t prime = 0x9e3779b9;

        auto hash_combine = [&](std::size_t v) {
            hash_value ^= v + prime + (hash_value << 6) + (hash_value >> 2);
        };

        // 处理 marking（区分标记：用质数 0x517cc1b7）
        hash_combine(0x517cc1b7 * marking.size());
        for (const auto &[place, tokens] : marking)
        {
            BOOST_ASSERT(tokens >= 0);
            if (tokens == 0)
                continue;
            std::size_t place_hash = std::hash<ptpn_v_desc>()(place);
            std::size_t token_hash = std::hash<int>()(tokens);
            hash_combine(place_hash);
            hash_combine(token_hash);
        }

        // 处理 enabled_runtimes（区分标记：用质数 0x5d5c8e11）
        hash_combine(0x5d5c8e11 * enabled_runtimes.size());
        for (const auto &[trans, interval] : enabled_runtimes)
        {
            std::size_t trans_hash = std::hash<ptpn_v_desc>()(trans);
            std::size_t lower_hash = std::hash<T>()(interval.lower);
            std::size_t upper_hash = std::hash<T>()(interval.upper);
            hash_combine(trans_hash);
            hash_combine(lower_hash);
            hash_combine(upper_hash);
        }

        // 处理 suspended_runtimes（区分标记：用质数 0x7a3d2a1f）
        hash_combine(0x7a3d2a1f * suspended_runtimes.size());
        for (const auto &[trans, interval] : suspended_runtimes)
        {
            std::size_t trans_hash = std::hash<ptpn_v_desc>()(trans);
            std::size_t lower_hash = std::hash<T>()(interval.lower);
            std::size_t upper_hash = std::hash<T>()(interval.upper);
            hash_combine(trans_hash);
            hash_combine(lower_hash);
            hash_combine(upper_hash);
        }

        return hash_value;
    }

    template <typename T>
    struct PriorityStateClassHash
    {
        std::size_t operator()(const PriorityStateClassT<T> &state) const
        {
            return state.hash();
        }
    };

} // namespace priority_scg

#endif // PPTPN_INCLUDE_PRIORITY_STATE_CLASS_H