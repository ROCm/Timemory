// MIT License
//
// Copyright (c) 2020, The Regents of the University of California,
// through Lawrence Berkeley National Laboratory (subject to receipt of any
// required approvals from the U.S. Dept. of Energy).  All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#if defined(TIMEMORY_USE_STATISTICS)
#    undef TIMEMORY_USE_STATISTICS
#endif

#define TIMEM_DEBUG
#define TIMEMORY_DISABLE_BANNER
#define TIMEMORY_DISABLE_STORE_ENVIRONMENT
#define TIMEMORY_DISABLE_CEREAL_CLASS_VERSION
#define TIMEMORY_DISABLE_COMPONENT_STORAGE_INIT
#define TIMEMORY_DISABLE_SETTINGS_SERIALIZATION

// disables unnecessary instantiations
#define TIMEMORY_COMPILER_INSTRUMENTATION

// tweaks settings
#define TIMEMORY_SETTINGS_PREFIX "TIMEM_"
#define TIMEMORY_PROJECT_NAME "timem"

#include "timemory/api/macros.hpp"

// create an API for the compiler instrumentation whose singletons will not be shared
// with the default timemory API
// TIMEMORY_DEFINE_NS_API(project, timem)

// define the API for all instantiations before including any more timemory headers
// #define TIMEMORY_API ::tim::project::timem

#include "timemory/api.hpp"
#include "timemory/macros.hpp"
#include "timemory/mpl/types.hpp"
#include "timemory/tpls/cereal/cereal/details/helpers.hpp"
#include "timemory/utility/macros.hpp"

#if defined(TIMEMORY_MACOS)
TIMEMORY_FORWARD_DECLARE_COMPONENT(page_rss)
TIMEMORY_DEFINE_CONCRETE_TRAIT(is_available, component::page_rss, false_type)
#endif

namespace tim
{
namespace trait
{
template <>
struct pretty_archive<void> : true_type
{};
}  // namespace trait
}  // namespace tim

#include "timemory/components/timing/child.hpp"
#include "timemory/general.hpp"
#include "timemory/mpl/concepts.hpp"
#include "timemory/sampling.hpp"
#include "timemory/timemory.hpp"
#include "timemory/tpls/cereal/cereal.hpp"

// clang-format off
TIMEMORY_DEFINE_CONCRETE_TRAIT(custom_label_printing, component::papi_vector, true_type)
//
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::wall_clock, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::user_clock, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::system_clock, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::cpu_clock, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::cpu_util, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::peak_rss, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::page_rss, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::virtual_memory, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::num_major_page_faults, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::num_minor_page_faults, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::priority_context_switch, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::voluntary_context_switch, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::read_char, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::read_bytes, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::written_char, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::written_bytes, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::network_stats, false_type)
TIMEMORY_DEFINE_CONCRETE_TRAIT(uses_value_storage, component::papi_vector, false_type)
// clang-format on

#include "md5.hpp"

// C includes
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#if defined(TIMEMORY_USE_LIBEXPLAIN)
#    include <libexplain/execvp.h>
#endif

#if defined(TIMEMORY_UNIX)
#    include <unistd.h>
extern "C"
{
    extern char** environ;
}
#endif

// C++ includes
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

template <typename Tp>
using vector_t       = std::vector<Tp>;
using stringstream_t = std::stringstream;
using mutex_t        = std::mutex;
using auto_lock_t    = std::unique_lock<mutex_t>;

using namespace tim::component;

template <typename Tp>
mutex_t&
type_mutex()
{
    return ::tim::type_mutex<Tp, TIMEMORY_API, 1, mutex_t>(0);
}

//--------------------------------------------------------------------------------------//
// create a custom component tuple printer
//
namespace tim
{
//
//--------------------------------------------------------------------------------------//
//
//                              OPERATION SPECIALIZATION
//
//--------------------------------------------------------------------------------------//
//
namespace operation
{
struct set_print_rank
{};
//
template <typename Tp>
struct print_properties
{
    print_properties(const Tp&, set_print_rank, int32_t _rank) { rank() = _rank; }
    static int32_t& rank()
    {
        static int32_t _v = -1;
        return _v;
    }
};
//
template <typename Tp>
struct custom_print
{
    using value_type = typename Tp::value_type;
    using base_type  = component::base<Tp, value_type>;

    custom_print(std::size_t N, std::size_t /*_Ntot*/, base_type& obj, std::ostream& os)
    {
        if(!tim::trait::runtime_enabled<Tp>::get())
            return;

        stringstream_t ss;
        if(N == 0)
            ss << std::endl;
        ss << "    ";
        if(print_properties<Tp>::rank() > -1)
            ss << print_properties<Tp>::rank() << "|> ";
        ss << obj << std::endl;
        os << ss.str();
    }
};
//
template <typename Tp>
struct custom_print<std::optional<Tp>>
{
    using value_type = typename Tp::value_type;
    using base_type  = component::base<Tp, value_type>;

    custom_print(std::size_t N, std::size_t _Ntot, std::optional<Tp>& obj,
                 std::ostream& os)
    {
        if(!tim::trait::runtime_enabled<Tp>::get() || !obj)
            return;

        custom_print<Tp>{ N, _Ntot, *obj, os };
    }
};
//
/// this is a custom version of base_printer for {read,written}_{char,bytes} components
template <typename Tp>
struct custom_base_printer
{
    using type       = Tp;
    using value_type = typename type::value_type;
    using base_type  = typename type::base_type;

    template <typename Up                                      = value_type,
              enable_if_t<!std::is_same<Up, void>::value, int> = 0>
    explicit custom_base_printer(std::ostream& _os, const type& _obj, int32_t _rank,
                                 const std::string& _label)
    {
        stringstream_t ss;
        stringstream_t ssv;
        stringstream_t ssr;
        stringstream_t ssrank;
        auto           _prec  = base_type::get_precision();
        auto           _width = base_type::get_width();
        auto           _flags = base_type::get_format_flags();
        auto           _disp  = _obj.get_display_unit();
        auto           _val   = _obj.get();

        ssv.setf(_flags);
        ssv << std::setw(_width) << std::setprecision(_prec) << std::get<0>(_val);
        if(!std::get<0>(_disp).empty())
            ssv << " " << std::get<0>(_disp);

        ss << ssv.str() << " " << _label;

        tim::consume_parameters(_rank);

        _os << ss.str();
    }

    template <typename Up = value_type, typename... Args,
              enable_if_t<std::is_same<Up, void>::value, int> = 0>
    explicit custom_base_printer(std::ostream&, const type&, Args&&...)
    {}
};
//
template <>
struct custom_base_printer<component::network_stats>
{
    using type       = component::network_stats;
    using value_type = typename type::value_type;
    using base_type  = typename type::base_type;

    template <typename... Args>
    custom_base_printer(std::ostream& _os, const type& _obj, int32_t _rank, Args&&...)
    {
        auto _prec   = base_type::get_precision();
        auto _width  = base_type::get_width();
        auto _flags  = base_type::get_format_flags();
        auto _units  = type::unit_array();
        auto _disp   = type::display_unit_array();
        auto _labels = type::label_array();
        auto _val    = _obj.load();

        auto _data = _val.get_data();
        for(size_t i = 0; i < _data.size(); ++i)
            _data.at(i) /= _units.at(i);

        std::vector<size_t> _order{};
        // print tx after rx
        for(size_t i = 0; i < _data.size() / 2; ++i)
        {
            _order.emplace_back(i);
            _order.emplace_back(i + _data.size() / 2);
        }
        // account for odd size if expanded
        if(_data.size() % 2 == 1)
            _order.emplace_back(_data.size() - 1);

        for(size_t i = 0; i < _data.size(); ++i)
        {
            auto           idx = _order.at(i);
            stringstream_t ss;
            stringstream_t ssv;
            stringstream_t ssrank;
            ssv.setf(_flags);
            ssv << std::setw(_width) << std::setprecision(_prec) << _data.at(idx);
            if(!_disp.at(idx).empty())
                ssv << " " << _disp.at(idx);

            if(i > 0)
            {
                ssrank << "\n    ";
                if(_rank > -1)
                {
                    ssrank << _rank << "|> ";
                }
            }

            auto _label = _labels.at(idx);
            _label =
                str_transform(_label, "rx_", "_", [](const std::string&) -> std::string {
                    return "network_receive";
                });
            _label =
                str_transform(_label, "tx_", "_", [](const std::string&) -> std::string {
                    return "network_transmit";
                });

            ss << ssrank.str() << ssv.str() << " " << _label;
            _os << ss.str();
        }
    }
};
//
#if defined(TIMEMORY_USE_PAPI)
//
template <>
struct custom_base_printer<component::papi_vector>
{
    using type       = component::papi_vector;
    using value_type = typename type::value_type;
    using base_type  = typename type::base_type;

    template <typename... Args>
    custom_base_printer(std::ostream& _os, const type& _obj, int32_t _rank, Args&&...)
    {
        auto _prec   = base_type::get_precision();
        auto _width  = base_type::get_width();
        auto _flags  = base_type::get_format_flags();
        auto _units  = _obj.unit_array();
        auto _disp   = _obj.display_unit_array();
        auto _labels = _obj.label_array();
        auto _data   = _obj.load();

        for(size_t i = 0; i < _data.size(); ++i)
        {
            stringstream_t ss;
            stringstream_t ssv;
            stringstream_t ssrank;
            ssv.setf(_flags);
            ssv << std::setw(_width) << std::setprecision(_prec) << _data.at(i);
            if(!_disp.at(i).empty())
                ssv << " " << _disp.at(i);

            if(i > 0)
            {
                ssrank << "\n    ";
                if(_rank > -1)
                {
                    ssrank << _rank << "|> ";
                }
            }

            auto _label = _labels.at(i);

            ss << ssrank.str() << ssv.str() << " " << _label;
            _os << ss.str();
        }
    }
};
//
#endif
//
#define CUSTOM_BASE_PRINTER_SPECIALIZATION(TYPE, LABEL)                                  \
    template <>                                                                          \
    struct base_printer<TYPE> : custom_base_printer<TYPE>                                \
    {                                                                                    \
        using type = TYPE;                                                               \
        static int32_t& rank() { return print_properties<TYPE>::rank(); }                \
        explicit base_printer(std::ostream& _os, const type& _obj)                       \
        : custom_base_printer<type>(_os, _obj, rank(), LABEL)                            \
        {}                                                                               \
    };
//
CUSTOM_BASE_PRINTER_SPECIALIZATION(component::read_bytes, "bytes_read")
CUSTOM_BASE_PRINTER_SPECIALIZATION(component::read_char, "char_read")
CUSTOM_BASE_PRINTER_SPECIALIZATION(component::written_bytes, "bytes_written")
CUSTOM_BASE_PRINTER_SPECIALIZATION(component::written_char, "char_written")
CUSTOM_BASE_PRINTER_SPECIALIZATION(component::network_stats, "")
//
#if defined(TIMEMORY_USE_PAPI)
//
CUSTOM_BASE_PRINTER_SPECIALIZATION(component::papi_vector, "")
//
#endif
//
}  // namespace operation
//
//--------------------------------------------------------------------------------------//
//
//                              COMPONENT SPECIALIZATION
//
//--------------------------------------------------------------------------------------//
//
namespace component
{
//
#if defined(TIMEMORY_USE_PAPI)
//
// template <>
// inline std::string
// papi_vector::get_display() const
// {
//     auto events = common_type::get_config()->event_names;
//     if(events.empty())
//         return "";
//     auto val          = load();
//     auto _get_display = [&](std::ostream& os, size_type idx) {
//         auto        _obj_value = val[idx];
//         auto        _evt_type  = events[idx];
//         std::string _label     = papi::get_event_info(_evt_type).short_descr;
//         std::string _disp      = papi::get_event_info(_evt_type).units;
//         auto        _prec      = base_type::get_precision();
//         auto        _width     = base_type::get_width();
//         auto        _flags     = base_type::get_format_flags();

//         stringstream_t ss, ssv, ssi;
//         ssv.setf(_flags);
//         ssv << std::setw(_width) << std::setprecision(_prec) << _obj_value;
//         if(!_disp.empty())
//             ssv << " " << _disp;
//         if(!_label.empty())
//             ssi << " " << _label;
//         if(idx > 0 && operation::print_properties<papi_vector>::rank() > -1)
//             ss << operation::print_properties<papi_vector>::rank() << "|> ";
//         ss << ssv.str() << ssi.str();
//         if(idx > 0)
//             os << "    ";
//         os << ss.str();
//     };

//     stringstream_t ss;
//     for(size_type i = 0; i < events.size(); ++i)
//     {
//         _get_display(ss, i);
//         if(i + 1 < events.size())
//             ss << '\n';
//     }

//     return ss.str();
// }
//
#endif
//
}  // namespace component
//
//--------------------------------------------------------------------------------------//
//
//                         VARIADIC WRAPPER SPECIALIZATION
//
//--------------------------------------------------------------------------------------//
//
/// \class tim::timem_tuple
/// \brief A specialized variadic component wrapper which inherits from the
/// lightweight_tuple which does not automatically push and pop to storage
///
template <typename... Types>
class timem_tuple : public lightweight_tuple<Types...>
{
public:
    using base_type       = lightweight_tuple<Types...>;
    using apply_v         = tim::mpl::apply<void>;
    using data_type       = typename base_type::impl_type;
    using this_type       = timem_tuple<Types...>;
    using clock_type      = std::chrono::system_clock;
    using time_point_type = typename clock_type::time_point;
    using hist_type       = std::pair<time_point_type, data_type>;

    template <template <typename> class Op, typename Tuple = data_type>
    using custom_operation_t =
        typename base_type::template custom_operation<Op, Tuple>::type;

public:
    TIMEMORY_DEFAULT_OBJECT(timem_tuple)

    explicit timem_tuple(string_view_cref_t key)
    : base_type{ key }
    {}

    timem_tuple(string_view_cref_t _key, data_type&& _data)
    : base_type{ _key }
    {
        m_data = std::move(_data);
    }

    using base_type::get;
    using base_type::get_labeled;
    using base_type::reset;
    using base_type::start;
    using base_type::stop;

    void set_output(std::ofstream* ofs) { m_ofs = ofs; }

    template <typename FuncT>
    void set_notify(FuncT&& _v)
    {
        tim::sampling::set_notify(m_notify, std::forward<FuncT>(_v));
    }

    void set_history(std::vector<hist_type>* _v) { m_data_hist = _v; }

    size_t get_buffer_size() const { return m_collect_size; }

    void set_buffer_size(size_t _v)
    {
        ::auto_lock_t _lk{ ::type_mutex<hist_type>(), std::defer_lock };
        if(!_lk.owns_lock())
            _lk.lock();
        m_collect_size = _v;
        m_collect_hist = (m_collect_size > 0);
        if(m_collect_hist)
        {
            m_hist_buff.reserve(std::max<size_t>(m_hist_buff.capacity(), _v));
        }
    }

    std::vector<hist_type>& swap_history(std::vector<hist_type>& _v)
    {
        ::auto_lock_t _lk{ ::type_mutex<hist_type>(), std::defer_lock };
        if(!_lk.owns_lock())
            _lk.lock();
        std::swap(m_hist_buff, _v);
        _lk.unlock();
        return _v;
    }

    template <typename... Args>
    void sample(Args&&... args)
    {
        if(base_type::m_is_active())
        {
            stop();
            base_type::sample(std::forward<Args>(args)...);
            if(m_collect_hist)
            {
                ::auto_lock_t _lk{ ::type_mutex<hist_type>(), std::defer_lock };
                if(!_lk.owns_lock())
                    _lk.lock();

                if(m_hist_buff.size() < m_collect_size)
                {
                    m_hist_buff.emplace_back(clock_type::now(), m_data);
                    if(m_hist_buff.size() + 1 >= m_collect_size)
                        m_notify(nullptr);
                }
            }
            if(m_ofs)
            {
                (*m_ofs) << get_local_datetime("[===== %r %F =====]\n") << *this
                         << std::endl;
            }
            start();
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const timem_tuple<Types...>& obj)
    {
        auto   ssp    = stringstream_t{};
        auto   ssd    = stringstream_t{};
        auto&& _data  = obj.m_data;
        auto&& _key   = obj.key();
        auto&& _width = obj.output_width();

        using cprint_t = custom_operation_t<operation::custom_print, data_type>;
        mpl::apply<void>::access_with_indices<cprint_t>(_data, std::ref(ssd));

        ssp << std::setw(_width) << std::left << _key;
        os << ssp.str() << ssd.str();

        if(&os != obj.m_ofs && obj.m_ofs)
        {
            *(obj.m_ofs) << get_local_datetime("[===== %r %F =====]\n", launch_time);
            *(obj.m_ofs) << ssp.str();
            *(obj.m_ofs) << ssd.str();
            *(obj.m_ofs) << std::endl;
        }

        return os;
    }

    void set_rank(int32_t _rank)
    {
        mpl::apply<void>::access<
            custom_operation_t<operation::print_properties, data_type>>(
            this->m_data, operation::set_print_rank{}, _rank);
    }

    TIMEMORY_NODISCARD bool empty() const { return m_empty; }

    template <typename Archive>
    void serialize(Archive& ar, const unsigned int)
    {
        auto _timestamp_str = [](const time_point_type& _tp) {
            char _repr[64];
            std::memset(_repr, '\0', sizeof(_repr));
            std::time_t _value = std::chrono::system_clock::to_time_t(_tp);
            // alternative: "%c %Z"
            if(std::strftime(_repr, sizeof(_repr), "%a %b %d %T %Y %Z",
                             std::localtime(&_value)))
                return std::string{ _repr };
            return std::string{};
        };

        using data_tuple_type = decay_t<decltype(m_data)>;
        constexpr auto N      = std::tuple_size<data_tuple_type>::value;
        serialize_tuple(ar, m_data, make_index_sequence<N>{});

        auto* _hist = (m_data_hist) ? m_data_hist : &m_hist_buff;
        ar.setNextName("history");
        ar.startNode();
        ar.makeArray();
        for(auto& itr : *_hist)
        {
            ar.startNode();
            ar.setNextName("sample_timestamp");
            ar.startNode();
            ar(cereal::make_nvp("localtime", _timestamp_str(itr.first)));
            ar(cereal::make_nvp("time_since_epoch",
                                itr.first.time_since_epoch().count()));
            ar.finishNode();
            serialize_tuple(ar, itr.second, make_index_sequence<N>{});
            ar.finishNode();
        }
        ar.finishNode();
    }

    template <typename Up, typename Tp = decay_t<Up>>
    static auto get_metadata_label()
    {
        auto _name = metadata<Tp>::name();
        for(auto&& itr : { "::", "child_" })
        {
            auto _pos = std::string::npos;
            while((_pos = _name.find(itr)) != std::string::npos)
            {
                _name = _name.substr(_pos + std::string(itr).length());
            }
        }
        _name = _name.substr(0, _name.find('<'));
        return _name;
    }

    template <typename Archive, typename Tp>
    static auto serialize_entry(Archive& ar, Tp&& _obj)
    {
        using value_type = tim::concepts::unqualified_type_t<Tp>;

        if constexpr(tim::is_optional<value_type>::value)
        {
            if(_obj)
                return serialize_entry(ar, *_obj);
        }
        else
        {
            if(!trait::runtime_enabled<decay_t<Tp>>::get())
                return;
            auto _name = get_metadata_label<Tp>();
            ar.setNextName(_name.c_str());
            ar.startNode();
            ar(cereal::make_nvp("value", _obj.get()));
            ar(cereal::make_nvp("repr", _obj.get_display()));
            ar(cereal::make_nvp("laps", _obj.get_laps()));
            ar(cereal::make_nvp("unit_value", _obj.get_unit()));
            ar(cereal::make_nvp("unit_repr", _obj.get_display_unit()));
            ar.finishNode();
            // ar(cereal::make_nvp(_name, std::forward<Tp>(_obj)));
        }
    }

    template <typename Archive, typename... Tp, size_t... Idx>
    static void serialize_tuple(Archive& ar, std::tuple<Tp...>& _data,
                                index_sequence<Idx...>)
    {
        TIMEMORY_FOLD_EXPRESSION(serialize_entry(ar, std::get<Idx>(_data)));
    }

private:
    using base_type::m_data;

    bool                       m_empty        = false;
    bool                       m_collect_hist = false;
    size_t                     m_collect_size = 0;
    std::ofstream*             m_ofs          = nullptr;
    std::function<void(bool*)> m_notify       = [](bool* _v) {
        if(_v)
            *_v = true;
    };
    std::vector<hist_type>*    m_data_hist = nullptr;
    std::vector<hist_type>     m_hist_buff = {};
    std::default_random_engine m_generator{ std::random_device{}() };

public:
    static std::time_t* launch_time;
};
//
template <typename... Types>
using timem_tuple_t = convert_t<mpl::available_t<type_list<Types...>>, timem_tuple<>>;
//
template <typename... Types>
std::time_t* timem_tuple<Types...>::launch_time = nullptr;
//
}  // namespace tim
//
//--------------------------------------------------------------------------------------//
//
TIMEMORY_DEFINE_NS_API(project, timem)  // provided by timemory API exclusively

#if !defined(TIMEM_BUNDLE)
#    define TIMEM_BUNDLE                                                                 \
        tim::timem_tuple_t<wall_clock, user_clock, system_clock, cpu_clock, cpu_util,    \
                           peak_rss, page_rss, virtual_memory, num_major_page_faults,    \
                           num_minor_page_faults, priority_context_switch,               \
                           voluntary_context_switch, read_char, read_bytes,              \
                           written_char, written_bytes, network_stats, papi_vector>
#endif

//
#if !defined(TIMEM_PID_SIGNAL)
#    define TIMEM_PID_SIGNAL SIGCONT
#endif
//
#if !defined(TIMEM_PID_SIGNAL_STRING)
#    define TIMEM_PID_SIGNAL_STRING TIMEMORY_STRINGIZE(TIMEM_PID_SIGNAL)
#endif
//
using timem_bundle_t  = TIMEM_BUNDLE;
using sampler_t       = tim::sampling::sampler<TIMEM_BUNDLE, 1>;
using sampler_array_t = typename sampler_t::array_type;
static_assert(std::is_same<sampler_array_t, std::array<timem_bundle_t, 1>>::value,
              "Error! Sampler array is not std::array<timem_bundle_t, 1>");
//
//--------------------------------------------------------------------------------------//
//
inline sampler_t*&
get_sampler()
{
    static sampler_t* _instance = nullptr;
    return _instance;
}
//
//--------------------------------------------------------------------------------------//
//
inline timem_bundle_t*
get_measure()
{
    return (get_sampler()) ? get_sampler()->get_last() : nullptr;
}
//
//--------------------------------------------------------------------------------------//
//
using sigaction_t = struct sigaction;
//
struct signal_handler
{
    sigaction_t m_custom_sigaction;
    sigaction_t m_original_sigaction;
};
//
//--------------------------------------------------------------------------------------//
//
inline signal_handler&
get_signal_handler(int _sig)
{
    static std::map<int, signal_handler> _instance{};
    auto                                 itr = _instance.find(_sig);
    if(itr == _instance.end())
    {
        auto eitr = _instance.emplace(_sig, signal_handler{});
        if(eitr.second)
            itr = eitr.first;
    }
    if(itr == _instance.end())
        throw std::runtime_error("Error! Signal handler for " + std::to_string(_sig) +
                                 " not found.");
    return itr->second;
}
//
//--------------------------------------------------------------------------------------//
//
std::vector<std::string_view>
get_environment_data();
//
//--------------------------------------------------------------------------------------//
//
struct timem_config
{
    static constexpr bool papi_available = tim::trait::is_available<papi_vector>::value;
    using hist_type                      = typename timem_bundle_t::hist_type;

    timem_config();
    ~timem_config() = default;
    TIMEMORY_DELETE_COPY_MOVE_OBJECT(timem_config)

    bool          use_papi       = tim::get_env("TIMEM_USE_PAPI", papi_available);
    bool          use_sample     = tim::get_env("TIMEM_SAMPLE", false);
    bool          debug          = tim::get_env("TIMEM_DEBUG", false);
    bool          completed      = false;
    bool          full_buffer    = false;
    int           verbose        = tim::get_env("TIMEM_VERBOSE", 0);
    int64_t       process_id     = tim::process::get_id();
    double        sample_freq    = tim::get_env<double>("TIMEM_SAMPLE_FREQ", 5.0);
    double        sample_delay   = tim::get_env<double>("TIMEM_SAMPLE_DELAY", 1.0e-6);
    size_t        buffer_size    = tim::get_env<size_t>("TIMEM_BUFFER_SIZE", 0);
    std::string   output_file    = tim::get_env<std::string>("TIMEM_OUTPUT", "");
    std::string   network_iface  = tim::get_env<std::string>("TIMEM_NETWORK_IFACE", "");
    std::string   executable     = {};
    std::set<int> signal_types   = { SIGALRM };
    std::set<int> signal_forward = { SIGINT };
    std::vector<std::string>      command       = tim::read_command_line(process_id);
    std::vector<std::string>      argvector     = {};
    std::vector<hist_type>        history       = {};
    std::unique_ptr<std::thread>  buffer_thread = {};
    std::condition_variable       buffer_cv     = {};
    std::vector<std::string_view> environment   = get_environment_data();
    std::vector<std::string>      papi_events =
        tim::delimit(tim::get_env<std::string>("TIMEM_PAPI_EVENTS", ""), " ,;\t");

    // template <typename Archive>
    // void serialize(Archive& ar, unsigned int);

    std::string get_output_filename(std::string        inp = {},
                                    const std::string& ext = {}) const;
};
//
//--------------------------------------------------------------------------------------//
//
inline timem_config&
get_config()
{
    static auto _instance = timem_config{};
    return _instance;
}
//
//--------------------------------------------------------------------------------------//
//
#define TIMEM_CONFIG_FUNCTION(NAME)                                                      \
    inline auto& NAME() { return get_config().NAME; }

TIMEM_CONFIG_FUNCTION(use_papi)
TIMEM_CONFIG_FUNCTION(use_sample)
TIMEM_CONFIG_FUNCTION(output_file)
TIMEM_CONFIG_FUNCTION(sample_freq)
TIMEM_CONFIG_FUNCTION(sample_delay)
TIMEM_CONFIG_FUNCTION(environment)
TIMEM_CONFIG_FUNCTION(process_id);
TIMEM_CONFIG_FUNCTION(debug)
TIMEM_CONFIG_FUNCTION(verbose)
TIMEM_CONFIG_FUNCTION(command)
TIMEM_CONFIG_FUNCTION(executable)
TIMEM_CONFIG_FUNCTION(buffer_size)
TIMEM_CONFIG_FUNCTION(signal_types)
TIMEM_CONFIG_FUNCTION(signal_forward)
TIMEM_CONFIG_FUNCTION(argvector)
TIMEM_CONFIG_FUNCTION(buffer_cv);
TIMEM_CONFIG_FUNCTION(buffer_thread);
TIMEM_CONFIG_FUNCTION(history);
TIMEM_CONFIG_FUNCTION(papi_events);
TIMEM_CONFIG_FUNCTION(completed);
TIMEM_CONFIG_FUNCTION(full_buffer);

#undef TIMEM_CONFIG_FUNCTION
//
//--------------------------------------------------------------------------------------//
//
// template <typename Archive>
// void
// timem_config::serialize(Archive& ar, unsigned int)
// {
// #define TIMEM_CONFIG_SERIALIZE(VAL) ar(tim::cereal::make_nvp(#VAL, VAL));

//     TIMEM_CONFIG_SERIALIZE(use_papi)
//     TIMEM_CONFIG_SERIALIZE(use_sample)
//     TIMEM_CONFIG_SERIALIZE(output_file)
//     TIMEM_CONFIG_SERIALIZE(sample_freq)
//     TIMEM_CONFIG_SERIALIZE(sample_delay)
//     TIMEM_CONFIG_SERIALIZE(environment)
//     TIMEM_CONFIG_SERIALIZE(process_id);
//     TIMEM_CONFIG_SERIALIZE(debug)
//     TIMEM_CONFIG_SERIALIZE(verbose)
//     TIMEM_CONFIG_SERIALIZE(command)
//     TIMEM_CONFIG_SERIALIZE(buffer_size)
//     TIMEM_CONFIG_SERIALIZE(signal_types)
//     TIMEM_CONFIG_SERIALIZE(signal_forward)
//     TIMEM_CONFIG_SERIALIZE(argvector)
//     TIMEM_CONFIG_SERIALIZE(papi_events)

// #undef TIMEM_CONFIG_SERIALIZE
// }
//
//--------------------------------------------------------------------------------------//
//
inline std::string
timem_config::get_output_filename(std::string inp, const std::string& ext) const
{
    if(inp.empty())
        inp = output_file;

    auto _rstrip = [](std::string& _inp, const std::string& _key) {
        auto pos = std::string::npos;
        while((pos = _inp.find(_key, _inp.length() - _key.length())) != std::string::npos)
            _inp = _inp.replace(pos, _key.length(), "");
    };

    if(!ext.empty())
    {
        _rstrip(inp, ext);
        _rstrip(inp, ".json");
        _rstrip(inp, ".txt");
    }

    if(!ext.empty())
        inp += ext;

    return tim::settings::format(inp, tim::settings::get_fallback_tag());
}

namespace tim
{
namespace cereal
{
template <typename Archive>
void
save(Archive& ar, const timem_config& data)
{
#define TIMEM_CONFIG_SERIALIZE(VAL) ar(make_nvp(#VAL, data.VAL));

    TIMEM_CONFIG_SERIALIZE(use_papi)
    TIMEM_CONFIG_SERIALIZE(use_sample)
    TIMEM_CONFIG_SERIALIZE(output_file)
    TIMEM_CONFIG_SERIALIZE(sample_freq)
    TIMEM_CONFIG_SERIALIZE(sample_delay)
    TIMEM_CONFIG_SERIALIZE(environment)
    TIMEM_CONFIG_SERIALIZE(process_id);
    TIMEM_CONFIG_SERIALIZE(debug)
    TIMEM_CONFIG_SERIALIZE(verbose)
    TIMEM_CONFIG_SERIALIZE(command)
    TIMEM_CONFIG_SERIALIZE(buffer_size)
    TIMEM_CONFIG_SERIALIZE(signal_types)
    TIMEM_CONFIG_SERIALIZE(signal_forward)
    TIMEM_CONFIG_SERIALIZE(argvector)
    TIMEM_CONFIG_SERIALIZE(papi_events)

#undef TIMEM_CONFIG_SERIALIZE
}

template <typename Archive>
void
save(Archive& ar, const std::vector<std::string_view>& data)
{
    ar(make_size_tag(static_cast<size_type>(data.size())));  // number of elements
    for(auto&& v : data)
        ar(std::string{ v });
}
}  // namespace cereal
}  // namespace tim
