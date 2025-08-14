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
        T upper; // 上界，如果是无穷大则使用类型的最大值表示

        // 构造函数
        explicit TimeIntervalT(T lower = T(0), T upper = std::numeric_limits<T>::max())
            : lower(lower), upper(upper) {}

        // TODO: 交集没有考虑全局时钟,尝试把等待时间设置为单数值
        TimeIntervalT intersect(const TimeIntervalT &other) const
        {
            return TimeIntervalT(std::max(lower, other.lower), std::min(upper, other.upper));
        }

        // 检查区间是否有效
        [[nodiscard]] bool is_valid() const { return lower <= upper && lower >= 0; }

        // 区间是否为空
        [[nodiscard]] bool is_empty() const { return !is_valid(); }

        // 检查区间是否包含某个值
        bool contains(const T &value) const
        {
            return lower <= value && value <= upper;
        }

        // 区间 位移（时间推移）
        TimeIntervalT shift(const T &offset) const
        {
            // 处理极限情况以避免溢出
            T new_lower = lower == std::numeric_limits<T>::min() && offset < 0 ? std::numeric_limits<T>::min() : lower + offset;

            T new_upper = upper == std::numeric_limits<T>::max() && offset > 0 ? std::numeric_limits<T>::max() : upper + offset;

            return TimeIntervalT(new_lower, new_upper);
        }

        // 相等比较
        bool operator==(const TimeIntervalT &other) const
        {
            return lower == other.lower && upper == other.upper;
        }

        // 不等比较
        bool operator!=(const TimeIntervalT &other) const
        {
            return !(*this == other);
        }

        // 字符串表示
        [[nodiscard]] std::string to_string() const
        {
            std::string upper_str = upper == std::numeric_limits<T>::max() ? "∞" : std::to_string(upper);
            return "[" + std::to_string(lower) + ", " + upper_str + "]";
        }
    };

    // 使用int类型的时间区间作为默认类型
    using TimeInterval = TimeIntervalT<int>;

    // 标记（Marking）表示Petri网中库所的token分布
    using Marking = std::map<ptpn_v_desc, int>; // 库所 -> token数量

    // 优先级时间 Petri 网的状态类
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
            // 转换std::pair到TimeIntervalT
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

        // 设置库所的token数量
        void set_token(const ptpn_v_desc place, const int tokens)
        {
            if (tokens > 0)
            {
                marking[place] = tokens;
            }
            else
            {
                marking.erase(place); // 移除token数为0的库所
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
                // 确保变迁不同时处于使能和挂起状态
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
                // 确保变迁不同时处于使能和挂起状态
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

        // 时间推移
        void elapse_time(T time_units)
        {
            if (time_units <= 0)
                return;

            // 更新使能变迁的运行时间区间
            for (auto &[trans, interval] : enabled_runtimes)
            {
                interval = interval.shift(-time_units); // 时间推移，区间下移
            }

            // 更新挂起变迁的运行时间区间
            for (auto &[trans, interval] : suspended_runtimes)
            {
                interval = interval.shift(-time_units); // 时间推移，区间下移
            }
        }

        [[nodiscard]] bool is_valid() const
        {
            // 检查所有区间是否有效
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

        // 计算状态类的哈希值（用于容器支持）
        [[nodiscard]] std::size_t hash() const;

        // 获取标记的字符串表示，用于调试
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

    private:
        Marking marking;                                         // 标记
        std::map<ptpn_v_desc, interval_type> enabled_runtimes;   // 使能变迁的运行时间
        std::map<ptpn_v_desc, interval_type> suspended_runtimes; // 挂起变迁的运行时间
    };

    // 使用int类型的状态类作为默认类型
    using PriorityStateClass = PriorityStateClassT<>;

    template <typename T>
    bool PriorityStateClassT<T>::operator==(const PriorityStateClassT &other) const
    {
        if (marking.size() != other.marking.size())
        {
            return false;
        }

        for (const auto &[fst, snd] : marking)
        {
            if (snd <= 0)
            {
                continue;
            }

            auto it = other.marking.find(fst);
            if (it == other.marking.end() || it->second != snd)
            {
                return false;
            }
        }

        if (enabled_runtimes.size() != other.enabled_runtimes.size())
        {
            return false;
        }

        for (const auto &[t, runtime] : enabled_runtimes)
        {
            auto it = other.enabled_runtimes.find(t);
            if (it == other.enabled_runtimes.end() || it->second != runtime)
            {
                return false;
            }
        }

        if (suspended_runtimes.size() != other.suspended_runtimes.size())
        {
            return false;
        }

        for (const auto &[t, runtime] : suspended_runtimes)
        {
            auto it = other.suspended_runtimes.find(t);
            if (it == other.suspended_runtimes.end() || it->second != runtime)
            {
                return false;
            }
        }

        return true;
    }

    template <typename T>
    std::string PriorityStateClassT<T>::to_string() const
    {
        std::stringstream ss;

        // 输出标记
        ss << "Marking: {";
        bool first = true;
        for (const auto &[place, tokens] : marking)
        {
            if (tokens <= 0)
                continue; // 仅显示有token的库所

            if (!first)
            {
                ss << ", ";
            }
            ss << "p" << place << ":" << tokens;
            first = false;
        }
        ss << "}\n";

        // 输出使能变迁的运行时间
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

        // 输出挂起变迁的运行时间
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

    // 哈希计算
    template <typename T>
    std::size_t PriorityStateClassT<T>::hash() const
    {
        std::size_t hash_value = 0;

        // Hash marking
        for (const auto &[place, tokens] : marking)
        {
            BOOST_ASSERT(tokens >= 0);
            if (tokens == 0)
                continue;
            hash_value ^= std::hash<ptpn_v_desc>()(place) ^ std::hash<int>()(tokens);
        }

        // Hash enabled runtimes
        for (const auto &[trans, interval] : enabled_runtimes)
        {
            hash_value ^= std::hash<ptpn_v_desc>()(trans) ^
                          std::hash<T>()(interval.lower) ^
                          std::hash<T>()(interval.upper);
        }

        // Hash suspended runtimes
        for (const auto &[trans, interval] : suspended_runtimes)
        {
            hash_value ^= std::hash<ptpn_v_desc>()(trans) ^
                          std::hash<T>()(interval.lower) ^
                          std::hash<T>()(interval.upper) ^ 0x12345678; // 与使能变迁区分
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