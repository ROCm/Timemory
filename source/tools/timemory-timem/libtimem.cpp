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

#include "libtimem.hpp"

#include "timemory/backends/process.hpp"
#include "timemory/components/network/components.hpp"
#include "timemory/components/papi/papi_vector.hpp"
#include "timemory/components/papi/types.hpp"
#include "timemory/log/color.hpp"
#include "timemory/settings/settings.hpp"
#include "timemory/utility/argparse.hpp"
#include "timemory/utility/join.hpp"
#include "timemory/utility/types.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//--------------------------------------------------------------------------------------//

extern "C"
{
    void          timemory_error_signal_handler(int signo, siginfo_t*, void*);
    extern char** environ;
}

//--------------------------------------------------------------------------------------//

timem_config::timem_config()
{
    if(!command.empty())
        executable = command.front();

    if(papi_events.empty())
        use_papi = false;

    if(sample_freq <= 0.0)
        use_sample = false;

    if(!network_iface.empty())
    {
        tim::settings::instance()->set(TIMEMORY_SETTINGS_KEY("NETWORK_INTERFACE"),
                                       network_iface, true);
        tim::trait::apply<tim::trait::runtime_enabled>::set<network_stats>(true);
    }
}

std::vector<std::string_view>
get_environment_data()
{
    auto   _data = std::vector<std::string_view>{};
    char*  val   = nullptr;
    size_t idx   = 0;
    do
    {
        val = environ[idx++];
        if(val)
            _data.emplace_back(val);
    } while(val != nullptr);

    return _data;
}

//--------------------------------------------------------------------------------------//

namespace
{
using sigaction_t      = struct sigaction;
using signal_func_t    = sighandler_t (*)(int signum, sighandler_t handler);
using sigaction_func_t = int (*)(int signum, const struct sigaction* __restrict__ act,
                                 struct sigaction* __restrict__ oldact);

constexpr auto timemory_num_signals = NSIG;
constexpr auto timemory_handled_signals =
    std::array<int, 7>{ SIGINT, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGTERM };

std::exception_ptr global_exception_pointer = nullptr;

void
handle_exception();

void
store_history(timem_bundle_t*);

void
parent_process(pid_t pid);

using main_func_t = int (*)(int, char**, char**);

main_func_t&
get_main_function()
{
    static main_func_t user_main = nullptr;
    return user_main;
}

signal_func_t&
get_signal_function()
{
    static signal_func_t user_signal = nullptr;
    return user_signal;
}

sigaction_func_t&
get_sigaction_function()
{
    static sigaction_func_t user_sigaction =
        (sigaction_func_t) dlsym(RTLD_NEXT, "sigaction");
    return user_sigaction;
}

struct chained_siginfo
{
    int                        signo   = 0;
    sighandler_t               handler = nullptr;
    std::optional<sigaction_t> action  = {};
};

auto&
get_chained_signals()
{
    static auto _v = std::array<std::optional<chained_siginfo>, timemory_num_signals>{};
    return _v;
}

bool
is_handled_signal(int signum)
{
    return std::any_of(timemory_handled_signals.begin(), timemory_handled_signals.end(),
                       [signum](auto val) { return (val == signum); });
}
}  // namespace

//--------------------------------------------------------------------------------------//

namespace
{
//--------------------------------------------------------------------------------------//

void
handle_exception()
{
    if(global_exception_pointer)
    {
        try
        {
            std::rethrow_exception(global_exception_pointer);
        } catch(const std::exception& ex)
        {
            std::cerr << "Thread exited with exception: " << ex.what() << "\n";
            std::rethrow_exception(global_exception_pointer);
        }
    }
}

//--------------------------------------------------------------------------------------//

void
store_history(timem_bundle_t* _bundle)
{
    using hist_type = typename timem_bundle_t::hist_type;

    auto _scompleted = []() { return completed() ? "y" : "n"; };
    auto _sfullbuff  = []() { return full_buffer() ? "y" : "n"; };
    tim::consume_parameters(_scompleted, _sfullbuff);

    static thread_local bool* notify_completed = nullptr;
    try
    {
        _bundle->set_notify([](bool* _completed) {
            full_buffer()    = true;
            notify_completed = _completed;
            buffer_cv().notify_one();
        });
        while(!completed())
        {
            std::vector<hist_type> _buff{};
            _buff.reserve(_bundle->get_buffer_size());

            TIMEMORY_CONDITIONAL_PRINT_HERE(
                debug() && verbose() > 2,
                "thread entering wait. completed: %s, full buffer: "
                "%s, buffer: %zu, history: %zu",
                _scompleted(), _sfullbuff(), _buff.size(), history().size());

            auto_lock_t _lk{ type_mutex<hist_type>() };
            buffer_cv().wait(_lk, []() { return completed() || full_buffer(); });

            TIMEMORY_CONDITIONAL_PRINT_HERE(
                debug() && verbose() > 2,
                "thread swapping history. completed: %s, full buffer: "
                "%s, buffer: %zu, history: %zu",
                _scompleted(), _sfullbuff(), _buff.size(), history().size());

            if(_lk.owns_lock())
                _lk.unlock();
            _buff = _bundle->swap_history(_buff);

            TIMEMORY_CONDITIONAL_PRINT_HERE(
                debug() && verbose() > 2,
                "thread transferring buffer contents. completed: %s, "
                "full buffer: %s, buffer: %zu, history: %zu",
                _scompleted(), _sfullbuff(), _buff.size(), history().size());

            // if the sampler requested to be notified when completed
            // set the value (held by sampler) to true and set the
            // local pointer to nullptr since the variable pointer to it
            // was probably allocated on stack
            if(notify_completed)
            {
                *notify_completed = true;
                notify_completed  = nullptr;
            }
            full_buffer() = false;
            history().reserve(history().size() + _buff.size());
            for(auto& itr : _buff)
                history().emplace_back(std::move(itr));
        }
        TIMEMORY_CONDITIONAL_PRINT_HERE(
            debug(), "thread completed. completed: %s, full buffer: %s, history: %zu",
            _scompleted(), _sfullbuff(), history().size());

        _bundle->set_buffer_size(0);
        _bundle->set_notify([]() {
            completed() = true;
            buffer_cv().notify_one();
        });

        TIMEMORY_CONDITIONAL_PRINT_HERE(
            debug(),
            "thread sorting history. completed: %s, full buffer: %s, history: %zu",
            _scompleted(), _sfullbuff(), history().size());

        std::sort(history().begin(), history().end(),
                  [](const hist_type& _lhs, const hist_type& _rhs) {
                      return _lhs.first.time_since_epoch().count() <
                             _rhs.first.time_since_epoch().count();
                  });

        TIMEMORY_CONDITIONAL_PRINT_HERE(
            debug(),
            "thread setting history. completed: %s, full buffer: %s, history: %zu",
            _scompleted(), _sfullbuff(), history().size());

        _bundle->set_history(&history());
    } catch(...)
    {
        // Set the global exception pointer in case of an exception
        global_exception_pointer = std::current_exception();
    }
}

//--------------------------------------------------------------------------------------//

void
parent_process(pid_t pid)
{
    // mark as completed and wake buffer thread so it can exit
    completed() = true;
    get_measure()->set_buffer_size(0);
    get_measure()->set_notify([]() {
        completed() = true;
        buffer_cv().notify_one();
    });

    if(buffer_thread())
    {
        using hist_type = typename timem_bundle_t::hist_type;
        auto_lock_t _lk{ type_mutex<hist_type>() };
        _lk.unlock();
        for(size_t i = 0; i < 10; ++i)
        {
            buffer_cv().notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
        }
        buffer_thread()->join();
        buffer_thread().reset();
        handle_exception();
    }

    auto _measurements = std::vector<timem_bundle_t>{};

    if(get_measure())
    {
        if((debug() && verbose() > 1) || verbose() > 2)
            std::cerr << "[AFTER STOP][" << pid << "]> " << *get_measure() << std::endl;

        TIMEMORY_CONDITIONAL_PRINT_HERE(debug(), "%s", "Getting serial measurement");
        _measurements = { *get_measure() };
    }
    else
    {
        TIMEMORY_CONDITIONAL_PRINT_HERE(debug(), "%s", "No measurements");
    }

    if(_measurements.empty())
    {
        TIMEMORY_CONDITIONAL_PRINT_HERE(debug(), "No measurements on rank %i. Returning",
                                        tim::mpi::rank());
        return;
    }

    auto _oss = stringstream_t{};
    for(size_t i = 0; i < _measurements.size(); ++i)
    {
        auto& itr = _measurements.at(i);
        if(itr.empty())
        {
            TIMEMORY_CONDITIONAL_PRINT_HERE(debug(), "%s (iteration: %zu)",
                                            "Empty measurement. Continuing", i);
            continue;
        }
        if(!_measurements.empty() && tim::mpi::size() > 1)
            itr.set_rank(i);

        TIMEMORY_CONDITIONAL_PRINT_HERE(debug(), "streaming iteration: %zu", i);
        _oss << itr << std::flush;
    }

    if(_oss.str().empty())
    {
        TIMEMORY_CONDITIONAL_PRINT_HERE(debug(), "%s", "Empty output. Returning");
        return;
    }

    if(output_file().empty())
    {
        std::cerr << '\n';
    }
    else
    {
        using json_type = tim::cereal::PrettyJSONOutputArchive;
        // extra function
        auto _cmdline = [](json_type& ar) {
            ar(tim::cereal::make_nvp("command_line", argvector()));
            ar(tim::cereal::make_nvp("config", get_config()));
        };

        auto fname = get_config().get_output_filename({}, ".json");
        fprintf(stderr, "%s%s[%s]> Outputting '%s'...\n%s", ::tim::log::color::source(),
                (verbose() < 0) ? "" : "\n", executable().c_str(), fname.c_str(),
                ::tim::log::color::end());
        tim::generic_serialization<json_type>(fname, _measurements, "timemory", "timem",
                                              _cmdline);
    }

    auto quiet = !output_file().empty() && verbose() < 0 && !debug();
    if(!quiet)
    {
        TIMEMORY_CONDITIONAL_PRINT_HERE(debug(), "%s", "reporting");
        tim::log::stream(std::cerr, tim::log::color::info()) << _oss.str() << "\n";
    }
    else
    {
        TIMEMORY_CONDITIONAL_PRINT_HERE(debug(), "%s", "reporting skipped (quiet)");
    }

    // tim::mpi::barrier();
    // tim::mpi::finalize();
}

//--------------------------------------------------------------------------------------//

template <typename Tp, typename Up = Tp,
          std::enable_if_t<tim::trait::is_available<Up>::value, int> = 0>
void
configure(bool _enable)
{
    if(_enable)
        Tp::configure();
}

template <typename Tp, typename Up = Tp,
          std::enable_if_t<!tim::trait::is_available<Up>::value, long> = 0>
void
configure(bool)
{}

//--------------------------------------------------------------------------------------//

// output file
auto ofs = std::unique_ptr<std::ofstream>{};

//--------------------------------------------------------------------------------------//

void
timem_init(int argc, char** argv)
{
    // parallel settings
    tim::settings::mpi_init()       = false;
    tim::settings::mpi_finalize()   = false;
    tim::settings::upcxx_init()     = false;
    tim::settings::upcxx_finalize() = false;
    // other settings
    tim::settings::banner()      = false;
    tim::settings::auto_output() = false;
    tim::settings::file_output() = false;
    tim::settings::ctest_notes() = false;
    tim::settings::scientific()  = false;
    tim::settings::width()       = 16;
    tim::settings::precision()   = 6;
    tim::settings::enabled()     = true;
    // ensure manager never writes metadata
    tim::manager::instance()->set_write_metadata(-1);

    auto& cfg = get_config();

    for(int i = 0; i < argc; ++i)
        argvector().emplace_back(argv[i]);

    // override a some settings
    tim::settings::suppress_parsing() = true;
    tim::settings::papi_threading()   = false;
    tim::settings::auto_output()      = false;
    tim::settings::output_prefix()    = "";

    // if(!use_sample() || signal_types().empty())
    // {
    //     tim::trait::apply<tim::trait::runtime_enabled>::set<
    //         page_rss, virtual_memory, read_char, read_bytes, written_char,
    //         written_bytes, papi_vector>(false);
    // }

    tim::trait::runtime_enabled<papi_vector>::set(!cfg.papi_events.empty());
    tim::trait::runtime_enabled<network_stats>::set(!cfg.network_iface.empty());

    if(!cfg.papi_events.empty())
        tim::settings::papi_events() = timemory::join::join(
            timemory::join::array_config{ " ", "", "" }, cfg.papi_events);

    if(!cfg.network_iface.empty())
        tim::settings::network_interface() = cfg.network_iface;

    // timem_bundle_t::get_initializer() = [&cfg](auto& _bundle) {
    //     if(!cfg.papi_events.empty())
    //         _bundle.template initialize<papi_vector>();
    //     if(!cfg.network_iface.empty())
    //         _bundle.template initialize<network_stats>(cfg.network_iface);
    // };

    auto compose_prefix = [&]() {
        auto _cmd = std::string{};
        {
            auto cmdss = std::stringstream{};
            for(const auto& itr : command())
                cmdss << " " << itr;
            _cmd = cmdss.str();
            if(_cmd.length() > 1)
                _cmd = _cmd.substr(1);
        }
        auto ss = stringstream_t{};
        ss << "[" << _cmd << "][PID=" << tim::process::get_id()
           << "]> Measurement totals";

        if(tim::dmp::size() > 1)
            ss << " (# ranks = " << tim::dmp::size() << "):";
        else
            ss << ":";

        return ss.str();
    };

    // sample_delay() = std::max<double>(sample_delay(), 1.0e-6);
    sample_freq() = std::min<double>(sample_freq(), 5000.);

    // ensure always enabled
    tim::settings::enabled() = true;

    configure<papi_vector>(use_papi());

    if(!output_file().empty())
    {
        auto output_dir = output_file().substr(0, output_file().find_last_of('/'));
        if(output_dir != output_file())
            tim::makedir(output_dir);
    }

    get_sampler() = new sampler_t{ compose_prefix() };
    if(use_sample() && !signal_types().empty())
    {
        get_measure()->set_buffer_size(buffer_size());
        buffer_thread() = std::make_unique<std::thread>(&store_history, get_measure());
    }

    if(!output_file().empty() && (debug() || verbose() > 1))
    {
        auto fname = get_config().get_output_filename({}, ".txt");
        ofs        = std::make_unique<std::ofstream>(fname.c_str());

        TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
                                        "Setting output file");
        get_measure()->set_output(ofs.get());
    }

    TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
                                    "configuring sampler");

    if(use_sample())
    {
        for(auto itr : signal_types())
        {
            get_sampler()->configure(tim::sampling::timer{
                itr, CLOCK_REALTIME, SIGEV_SIGNAL, sample_freq(), sample_delay() });
        }
    }

    TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "starting sampler");
    get_sampler()->start();

    if(get_measure() && ((debug() && verbose() > 1) || verbose() > 2))
        std::cerr << "[AFTER START][" << tim::process::get_id() << "]> " << *get_measure()
                  << std::endl;
}

void
timem_fini()
{
    TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "stopping sampler");
    get_sampler()->stop();

    TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "ignoring signals");
    get_sampler()->ignore(signal_types());

    TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "processing");
    parent_process(tim::process::get_id());

    if(get_measure())
        get_measure()->set_output(nullptr);

    delete get_sampler();

    TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "Completed");
}

auto time_buffer = std::array<std::byte, sizeof(std::time_t)>{};
}  // namespace

#if !defined(TIMEMORY_INTERNAL_API)
#    define TIMEMORY_INTERNAL_API __attribute__((visibility("internal")));
#endif

extern "C"
{
    void timemory_set_main(main_func_t main_func) TIMEMORY_INTERNAL_API;

    void timemory_error_signal_handler(int signo, siginfo_t* info, void* ucontext)
    {
        if(get_chained_signals().at(signo))
        {
            auto& _chained = *get_chained_signals().at(signo);
            if(_chained.action)
            {
                if((_chained.action->sa_flags & SA_SIGINFO) == SA_SIGINFO &&
                   _chained.action->sa_sigaction)
                {
                    _chained.action->sa_sigaction(signo, info, ucontext);
                }
                else if((_chained.action->sa_flags & SA_SIGINFO) != SA_SIGINFO &&
                        _chained.action->sa_handler)
                {
                    _chained.action->sa_handler(signo);
                }
            }
            else
            {
                if(_chained.handler)
                {
                    _chained.handler(signo);
                }
            }
        }

        ::raise(signo);
    }

    int timemory_main(int argc, char** argv, char** envp) TIMEMORY_INTERNAL_API;

    sighandler_t timemory_signal(int signum, sighandler_t handler) TIMEMORY_INTERNAL_API;

    int timemory_sigaction(int signum, const struct sigaction* __restrict__ act,
                           struct sigaction* __restrict__ oldact) TIMEMORY_INTERNAL_API;

    void timemory_set_main(main_func_t main_func) { get_main_function() = main_func; }

    sighandler_t timemory_signal(int signum, sighandler_t handler)
    {
        static auto _once = std::once_flag{};
        std::call_once(_once, []() {
            get_signal_function() = (signal_func_t) dlsym(RTLD_NEXT, "signal");
        });

        if(!is_handled_signal(signum))
            return get_signal_function()(signum, handler);

        get_chained_signals().at(signum) =
            chained_siginfo{ signum, handler, std::nullopt };

        return get_signal_function()(signum, [](int signum_v) {
            timemory_error_signal_handler(signum_v, nullptr, nullptr);
        });
    }

    int timemory_sigaction(int signum, const struct sigaction* __restrict__ act,
                           struct sigaction* __restrict__ oldact)
    {
        static auto _once = std::once_flag{};
        std::call_once(_once, []() {
            get_sigaction_function() = (sigaction_func_t) dlsym(RTLD_NEXT, "sigaction");
        });

        if(!is_handled_signal(signum))
            return get_sigaction_function()(signum, act, oldact);

        get_chained_signals().at(signum) = chained_siginfo{ signum, nullptr, *act };

        struct sigaction _upd_act = *act;
        _upd_act.sa_flags |= (SA_SIGINFO | SA_RESETHAND | SA_NODEFER);
        _upd_act.sa_sigaction = &timemory_error_signal_handler;

        return get_sigaction_function()(signum, &_upd_act, oldact);
    }

    int timemory_main(int argc, char** argv, char** envp)
    {
        timem_bundle_t::launch_time =
            new(time_buffer.data()) std::time_t{ *tim::settings::get_launch_time() };

        auto _dtor = tim::scope::destructor{ []() { timem_fini(); } };

        tim::timemory_init(argc, argv);
        timem_init(argc, argv);

        return get_main_function()(argc, argv, envp);
    }
}
