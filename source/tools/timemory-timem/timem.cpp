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

#define _GNU_SOURCE 1
#define _DEFAULT_SOURCE 1

#define TIMEMORY_DISABLE_BANNER
#define TIMEMORY_DISABLE_STORE_ENVIRONMENT
#define TIMEMORY_DISABLE_CEREAL_CLASS_VERSION
#define TIMEMORY_DISABLE_COMPONENT_STORAGE_INIT
#define TIMEMORY_DISABLE_SETTINGS_SERIALIZATION

// disables unnecessary instantiations
#define TIMEMORY_COMPILER_INSTRUMENTATION

#include "timemory/backends/process.hpp"
#include "timemory/components/network/components.hpp"
#include "timemory/components/papi/types.hpp"
#include "timemory/environment.hpp"
#include "timemory/log/color.hpp"
#include "timemory/settings.hpp"
#include "timemory/utility/argparse.hpp"
#include "timemory/utility/delimit.hpp"
#include "timemory/utility/types.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <csignal>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

template <typename Tp>
using vector_t       = std::vector<Tp>;
using stringstream_t = std::stringstream;
using string_t       = std::string;
using mutex_t        = std::mutex;
using auto_lock_t    = std::unique_lock<mutex_t>;

using namespace tim::component;

namespace fs = ::std::filesystem;

std::string
get_realpath(const std::string& _v)
{
    auto* _tmp  = ::realpath(_v.c_str(), nullptr);
    auto  _dtor = tim::scope::destructor{ [&_tmp]() { ::free(_tmp); } };
    if(_tmp)
    {
        auto _ret = std::string{ _tmp };
        return _ret;
    }

    auto _path = fs::path{ _v };
    return (fs::exists(_path)) ? fs::canonical(_path) : fs::absolute(_path);
}

std::string
get_internal_libpath(const std::string& _lib)
{
    auto _exe = get_realpath("/proc/self/exe");
    auto _pos = _exe.find_last_of('/');
    auto _dir = std::string{ "./" };
    if(_pos != std::string_view::npos)
        _dir = _exe.substr(0, _pos);
    return timemory::join::join("/", _dir, "..", "lib", _lib);
}

enum update_mode : int
{
    UPD_REPLACE = 0x1,
    UPD_PREPEND = 0x2,
    UPD_APPEND  = 0x3,
    UPD_WEAK    = 0x4,
};

auto updated_envs  = std::set<std::string_view>{};
auto original_envs = std::set<std::string>{};

template <typename Tp>
void
update_env(std::vector<char*>& _environ, std::string_view _env_var, Tp&& _env_val,
           update_mode&& _mode, std::string_view _join_delim = ":")
{
    updated_envs.emplace(_env_var);

    auto _prepend  = (_mode & UPD_PREPEND) == UPD_PREPEND;
    auto _append   = (_mode & UPD_APPEND) == UPD_APPEND;
    auto _weak_upd = (_mode & UPD_WEAK) == UPD_WEAK;

    auto _key = timemory::join::join("", _env_var, "=");
    for(auto& itr : _environ)
    {
        if(!itr)
            continue;
        if(std::string_view{ itr }.find(_key) == 0)
        {
            if(_weak_upd)
            {
                // if the value has changed, do not update but allow overridding the value
                // inherited from the initial env
                if(original_envs.find(std::string{ itr }) == original_envs.end())
                    return;
            }

            if(_prepend || _append)
            {
                if(std::string_view{ itr }.find(timemory::join::join("", _env_val)) ==
                   std::string_view::npos)
                {
                    auto _val = std::string{ itr }.substr(_key.length());
                    ::free(itr);
                    if(_prepend)
                        itr = ::strdup(
                            timemory::join::join(
                                '=', _env_var,
                                timemory::join::join(_join_delim, _val, _env_val))
                                .c_str());
                    else
                        itr = ::strdup(
                            timemory::join::join(
                                '=', _env_var,
                                timemory::join::join(_join_delim, _env_val, _val))
                                .c_str());
                }
            }
            else
            {
                ::free(itr);
                itr = ::strdup(timemory::join::join('=', _env_var, _env_val).c_str());
            }
            return;
        }
    }

    _environ.emplace_back(
        ::strdup(timemory::join::join('=', _env_var, _env_val).c_str()));
}

std::vector<char*>
get_environment()
{
    auto _env = std::vector<char*>{};
    if(environ != nullptr)
    {
        int idx = 0;
        while(environ[idx] != nullptr)
        {
            auto* _v = environ[idx++];
            original_envs.emplace(_v);
            _env.emplace_back(::strdup(_v));
        }
    }

    auto _timem_libpath = get_realpath(get_internal_libpath("libtimem.so"));

    update_env(_env, "TIMEMORY_LIBRARY_CTOR", "0", UPD_REPLACE);
    update_env(_env, "LD_PRELOAD", _timem_libpath, UPD_APPEND);
    update_env(_env, "LD_LIBRARY_PATH", fs::path{ _timem_libpath }.parent_path().string(),
               UPD_APPEND);

    return _env;
}

void
print_command(const std::vector<char*>& _argv, std::string_view _prefix)
{
    std::cout << tim::log::color::info() << _prefix << "Executing '"
              << timemory::join::join(timemory::join::array_config{ " " }, _argv)
              << "'...\n";

    std::cerr << tim::log::color::end() << std::flush;
}

void
print_updated_environment(std::vector<char*> _env, std::string_view _prefix)
{
    std::sort(_env.begin(), _env.end(), [](auto* _lhs, auto* _rhs) {
        if(!_lhs)
            return false;
        if(!_rhs)
            return true;
        return std::string_view{ _lhs } < std::string_view{ _rhs };
    });

    std::vector<std::string_view> _updates = {};
    std::vector<std::string_view> _general = {};

    for(auto* itr : _env)
    {
        if(itr == nullptr)
            continue;

        auto _is_timem = (std::string_view{ itr }.find("TIMEM") == 0);
        auto _updated  = false;
        for(const auto& vitr : updated_envs)
        {
            if(std::string_view{ itr }.find(vitr) == 0)
            {
                _updated = true;
                break;
            }
        }

        if(_updated)
            _updates.emplace_back(itr);
        else if(_is_timem)
            _general.emplace_back(itr);
    }

    if(_general.size() + _updates.size() == 0)
        return;

    std::cerr << std::endl;

    for(auto& itr : _general)
        std::cerr << tim::log::color::source() << _prefix << itr << "\n";
    for(auto& itr : _updates)
        std::cerr << tim::log::color::source() << _prefix << itr << "\n";

    std::cerr << tim::log::color::end() << std::flush;
}

int
main(int argc, char** argv)
{
    auto _env = get_environment();

    using parser_t     = tim::argparse::argument_parser;
    using parser_err_t = typename parser_t::result_type;

    auto help_check = [](parser_t& p, int _argc, char** _argv) {
        std::set<std::string> help_args = { "-h", "--help", "-?" };
        return (p.exists("help") || _argc == 1 ||
                (_argc > 1 && help_args.find(_argv[1]) != help_args.end()));
    };

    auto _pec        = EXIT_SUCCESS;
    auto help_action = [&_pec, argc, argv](parser_t& p) {
        if(_pec != EXIT_SUCCESS)
        {
            auto msg = std::stringstream{};
            msg << "Error in command:";
            for(int i = 0; i < argc; ++i)
                msg << " " << argv[i];
            msg << "\n\n";
            std::cerr << msg.str() << std::flush;
        }

        if(tim::dmp::rank() == 0)
        {
            stringstream_t hs;
            hs << "-- <CMD> <ARGS>\n\n";
            hs << "Examples:\n";
            hs << "    timem sleep 2\n";
            hs << "    timem -s /bin/zsh -- find /usr\n";
            hs << "    timemory-avail -H | grep PAPI | grep -i cache\n";
            hs << "    srun -N 1 -n 1 timem -e PAPI_L1_TCM PAPI_L2_TCM PAPI_L3_TCM -- "
                  "./myexe\n";
            p.print_help(hs.str());
        }
        exit(_pec);
    };

    auto parser = parser_t(argv[0]);

    parser.enable_help();
    parser.on_error([=, &_pec](parser_t& p, const parser_err_t& _err) {
        std::cerr << _err << std::endl;
        _pec = EXIT_FAILURE;
        help_action(p);
    });

    parser.add_argument()
        .names({ "--debug" })
        .description("Debug output")
        .count(0)
        .action(
            [&_env](parser_t&) { update_env(_env, "TIMEM_DEBUG", true, UPD_REPLACE); });
    parser.add_argument()
        .names({ "-v", "--verbose" })
        .description("Verbose output")
        .max_count(1)
        .action([&_env](parser_t& p) {
            if(p.get_count("verbose") == 0)
                update_env(_env, "TIMEM_VERBOSE", 1, UPD_REPLACE);
            else
                update_env(_env, "TIMEM_VERBOSE", p.get<int>("verbose"), UPD_REPLACE);
        });
    parser.add_argument({ "-N", "--monochrome" }, "Disable colorized output")
        .max_count(1)
        .dtype("bool")
        .action([&_env](parser_t& p) {
            update_env(_env, "TIMEMORY_MONOCHROME", p.get<bool>("monochrome"),
                       UPD_REPLACE);
        });
    parser.add_argument({ "-q", "--quiet" }, "Suppress as much reporting as possible")
        .count(0)
        .action([&_env](parser_t&) {
            update_env(_env, "TIMEM_DEBUG", false, UPD_REPLACE);
            update_env(_env, "TIMEM_VERBOSE", -1, UPD_REPLACE);
        });
    parser
        .add_argument({ "-d", "--sample-delay" },
                      "Set the delay before the sampler starts (seconds)")
        .count(1)
        .action([&_env](parser_t& p) {
            update_env(_env, "TIMEM_SAMPLE_DELAY", p.get<double>("sample-delay"),
                       UPD_REPLACE);
        });
    parser
        .add_argument({ "-f", "--sample-freq" }, "Set the frequency of the sampler "
                                                 "(number of interrupts per second)")
        .count(1)
        .action([&_env](parser_t& p) {
            update_env(_env, "TIMEM_SAMPLE_FREQ", p.get<double>("sample-freq"),
                       UPD_REPLACE);
        });
    parser
        .add_argument(
            { "--disable-sample", "--disable-sampling" },
            "Disable UNIX signal-based sampling. Sampling is the most common culprit for "
            "timem hanging (i.e. failing to exit after the child process exits)")
        .count(0)
        .action(
            [&_env](parser_t&) { update_env(_env, "TIMEM_SAMPLE", false, UPD_REPLACE); });
    parser
        .add_argument({ "-b", "--buffer-size" },
                      "If set to value > 0, timem will record a history of every sample. "
                      "This requires spawning an extra thread which will periodically "
                      "wake and flush the buffer.")
        .count(1)
        .dtype("size_t")
        .action([&_env](parser_t& p) {
            update_env(_env, "TIMEM_BUFFER_SIZE", p.get<size_t>("buffer-size"),
                       UPD_REPLACE);
        });
    parser
        .add_argument({ "-e", "--events", "--papi-events" },
                      "Set the hardware counter events to record (ref: `timemory-avail "
                      "-H | grep PAPI`)")
        .action([&_env](parser_t& p) {
            if(p.get_count("events") > 0)
            {
                if(!tim::trait::is_available<papi_array_t>::value)
                    throw std::runtime_error(
                        "Error! timemory was not built with PAPI support");
            }

            auto evts = p.get<std::vector<std::string>>("events");
            auto ss   = stringstream_t{};
            for(const auto& itr : evts)
                ss << ", " << itr;
            auto _events = ss.str();
            if(!_events.empty())
                _events = _events.substr(2);
            update_env(_env, "TIMEM_PAPI_EVENTS", _events, UPD_REPLACE);
        });

    auto output_help = std::string{
        R"(Write results to JSON output file.
%{INDENT}%Use:
%{OUTPUT_KEYS}%
%{INDENT}%E.g. '-o timem-output-%p'.
%{INDENT}%If verbosity >= 2 or debugging is enabled, will also write sampling data to log file.)"
    };

    auto _keys_help = std::stringstream{};
    for(const auto& itr : tim::settings::output_keys(tim::settings::get_fallback_tag()))
        _keys_help << "%{INDENT}%- '" << itr.key << "' to encode " << itr.description
                   << " (value=" << itr.value << ")\n";

    auto _replace = [](std::string& _inp, std::string_view _substr,
                       const std::string& _repl) -> std::string& {
        if(auto _pos = _inp.find(_substr); _pos != std::string::npos)
            return _inp.replace(_pos, _substr.length(), _repl);
        return _inp;
    };

    _replace(output_help, "%{OUTPUT_KEYS}%", _keys_help.str());

    parser.add_argument({ "-o", "--output" }, output_help)
        .max_count(1)
        .action([&_env](parser_t& p) {
            update_env(_env, "TIMEM_OUTPUT", p.get<std::string>("output"), UPD_REPLACE);
        });

    auto network_interfaces = network_stats::get_interfaces();
    auto default_net_iface  = tim::settings::instance()->get<std::string>(
        TIMEMORY_SETTINGS_KEY("NETWORK_INTERFACE"), true);
    if(!network_interfaces.empty())
    {
        parser.add_argument()
            .names({ "--network-stats" })
            .description("Enable sampling network usage statistics for the given "
                         "interface (Linux only)")
            .count(1)
            .choices(network_interfaces)
            .action([&](parser_t& p) {
                auto _iface = default_net_iface;
                if(_iface.empty() && !network_interfaces.empty())
                    _iface = network_interfaces.front();
                if(p.get_count("network-stats") > 0)
                    _iface = p.get<std::string>("network-stats");

                auto _exists =
                    std::any_of(network_interfaces.begin(), network_interfaces.end(),
                                [&_iface](const auto& itr) { return (itr == _iface); });
                if(!_exists)
                    throw std::runtime_error{ "invalid network interface" };

                update_env(_env, "TIMEM_NETWORK_IFACE", _iface, UPD_REPLACE);
            });
    }

    auto _arg = std::vector<char*>{};

    {
        auto  _args = parser.parse_known_args(argc, argv);
        auto  _argc = std::get<1>(_args);
        auto* _argv = std::get<2>(_args);

        if(help_check(parser, _argc, _argv))
            help_action(parser);

        _arg.reserve(_argc);
        for(int i = 1; i < _argc; ++i)
            _arg.emplace_back(_argv[i]);
    }

    print_updated_environment(_env, "0: ");
    print_command(_arg, "0: ");

    _arg.emplace_back(nullptr);
    _env.emplace_back(nullptr);

    return execvpe(_arg.front(), _arg.data(), _env.data());

    // // sample_delay() = std::max<double>(sample_delay(), 1.0e-6);
    // sample_freq() = std::min<double>(sample_freq(), 5000.);

    //     // override a some settings
    //     tim::settings::suppress_parsing() = true;
    //     tim::settings::papi_attach()      = true;
    //     tim::settings::papi_threading()   = false;
    //     tim::settings::auto_output()      = false;
    //     tim::settings::output_prefix()    = "";

    //     auto compose_prefix = [&]() {
    //         stringstream_t ss;
    // #if defined(TIMEMORY_USE_MPI)
    //         ss << "[" << command().c_str() << "]";
    //         if(!use_mpi())
    //             ss << "[PID=" << tim::process::get_id() << "]";
    //         ss << "> Measurement totals";
    // #else
    //         ss << "[" << command().c_str() << "]> Measurement totals";
    // #endif
    //         if(use_mpi())
    //         {
    //             ss << " (# ranks = " << tim::mpi::size() << "):";
    //         }
    //         else if(tim::dmp::size() > 1)
    //         {
    //             ss << " (# ranks = " << tim::dmp::size() << "):";
    //         }
    //         else
    //         {
    //             ss << ":";
    //         }
    //         return ss.str();
    //     };

    //     if(_argc > 1)
    //     {
    //         // e.g. timem mycmd
    //         command() = std::string(const_cast<const char*>(_argv[1]));
    //     }
    //     else
    //     {
    //         command()              = std::string(const_cast<const char*>(_argv[0]));
    //         tim::get_rusage_type() = RUSAGE_CHILDREN;
    //         exit(EXIT_SUCCESS);
    //     }

    //     if(parser.exists("output"))
    //     {
    //         auto ofname = parser.get<string_t>("output");
    //         if(ofname.empty())
    //         {
    //             auto _cmd = command();
    //             auto _pos = _cmd.find_last_of('/');
    //             if(_pos != std::string::npos)
    //                 _cmd = _cmd.substr(_pos + 1);
    //             ofname = TIMEMORY_JOIN("", argv[0], "-output", '/', _cmd);
    //             fprintf(stderr, "[%s]> No output filename provided. Using '%s'...\n",
    //                     command().c_str(), ofname.c_str());
    //         }
    //         output_file()   = ofname;
    //         auto output_dir = output_file().substr(0, output_file().find_last_of('/'));
    //         if(output_dir != output_file())
    //             tim::makedir(output_dir);
    //     }

    //     if(tim::settings::papi_events().empty())
    //     {
    //         if(use_papi())
    //         {
    //             tim::settings::papi_events() = "PAPI_TOT_CYC";
    //         }
    //         else
    //         {
    //             tim::trait::runtime_enabled<papi_array_t>::set(false);
    //         }
    //     }

    //     tim::get_rusage_type() = RUSAGE_CHILDREN;
    //     if(!use_sample())
    //         signal_types().clear();

    //     for(auto itr : signal_forward())
    //     {
    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 0),
    //                                         "timem will forward signal %i to its worker
    //                                         " "process if it is sent to this process
    //                                         (PID: %i)", itr, (int)
    //                                         tim::process::get_id());
    //         if(signal_types().count(itr) > 0)
    //             throw std::runtime_error(
    //                 TIMEMORY_JOIN(" ", "Error! timem sampler is using signal", itr,
    //                               "to handle the sampling measurements. Cannot forward
    //                               it. " "Re-run timem with the '--disable-sampling'
    //                               option to " "forward this signal"));
    //     }

    //     // set the signal handler on this process if using mpi so that we can read
    //     // the file providing the PID. If not, fork provides the PID so this is
    //     // unnecessary
    //     if(use_mpi())
    //     {
    //         create_signal_handler(TIMEM_PID_SIGNAL,
    //         get_signal_handler(TIMEM_PID_SIGNAL),
    //                               &childpid_catcher);
    //         // allocate the signal handlers in map
    //         for(auto itr : signal_forward())
    //             (void) get_signal_handler(itr);
    //     }
    //     else
    //     {
    //         for(auto itr : signal_forward())
    //             create_signal_handler(itr, get_signal_handler(itr), &forward_signal);
    //     }

    //     for(int i = 0; i < _argc; ++i)
    //         argvector().emplace_back(_argv[i]);

    //     //----------------------------------------------------------------------------------//
    //     //
    //     //          Create subprocesses
    //     //
    //     //----------------------------------------------------------------------------------//

    //     pid_t pid = (use_mpi()) ? master_pid() : fork();

    //     // SIGCHLD notifies the parent process when a child process exits, is
    //     interrupted, or
    //     // resumes after being interrupted
    //     // if(!use_mpi())
    //     //    signal(SIGCHLD, childpid_catcher);

    //     using comm_t        = tim::mpi::comm_t;
    //     comm_t comm_world_v = tim::mpi::comm_world_v;

    //     if(!use_sample() || signal_types().empty())
    //     {
    //         tim::trait::apply<tim::trait::runtime_enabled>::set<
    //             page_rss, virtual_memory, read_char, read_bytes, written_char,
    //             written_bytes, papi_array_t>(false);
    //     }

    // #if defined(TIMEMORY_USE_MPI)
    //     comm_t comm_child_v;
    //     //
    //     if(use_mpi())
    //     {
    //         tim::trait::apply<tim::trait::runtime_enabled>::set<
    //             child_user_clock, child_system_clock, child_cpu_clock, child_cpu_util,
    //             peak_rss, num_major_page_faults, num_minor_page_faults,
    //             priority_context_switch, voluntary_context_switch,
    //             papi_array_t>(false);

    //         using info_t      = tim::mpi::info_t;
    //         using argvector_t = tim::argparse::argument_vector;
    //         using cargs_t     = typename argvector_t::cargs_t;

    //         string_t pidexe = argv[0];
    //         // name of the executable should be some path + "timem"
    //         if(pidexe.substr(pidexe.find_last_of('/') + 1) == "timem")
    //         {
    //             // append "timem" + "ory-pid" to form "timemory-pid"
    //             pidexe += "ory-pid";
    //         }
    //         else if(pidexe.substr(pidexe.find_last_of('/') + 1) == "timem-mpi")
    //         {
    //             // remove "-mpi" -> "timem" + "ory-pid" to form "timemory-pid"
    //             pidexe = pidexe.substr(0, pidexe.length() - 4) + "ory-pid";
    //         }
    //         else
    //         {
    //             if(verbose() > -1)
    //             {
    //                 fprintf(stderr,
    //                         "Warning! Executable '%s' was expected to be 'timem'. Using
    //                         "
    //                         "'timemory-pid' instead of adding 'ory-pid' to name of "
    //                         "executable",
    //                         argv[0]);
    //             }
    //             // otherwise, assume it can find 'timemory-pid'
    //             pidexe = "timemory-pid";
    //         }

    //         auto comm_size  = tim::mpi::size();
    //         auto comm_rank  = tim::mpi::rank();
    //         auto pids       = std::vector<int>(comm_size, 0);
    //         auto procs      = std::vector<int>(comm_size, 1);
    //         auto errcodes   = std::vector<int>(comm_size, 0);
    //         auto infos      = std::vector<info_t>(comm_size, tim::mpi::info_null_v);
    //         auto margv      = std::vector<argvector_t>(comm_size, argvector_t(_argc,
    //         _argv)); auto cargv      = std::vector<cargs_t>{}; auto cargv_arg0 =
    //         std::vector<char*>{}; auto cargv_argv = std::vector<char**>{};

    //         pids.at(comm_rank) = pid;
    //         tim::mpi::gather(&pid, 1, MPI_INT, pids.data(), 1, MPI_INT, 0,
    //                          tim::mpi::comm_world_v);

    //         if(debug() && comm_rank == 0)
    //         {
    //             std::stringstream ss;
    //             std::cout << "[" << command() << "]> parent pids: ";
    //             for(const auto& itr : pids)
    //                 ss << ", " << itr;
    //             std::cout << ss.str().substr(2) << '\n';
    //         };

    //         // create the
    //         for(decltype(comm_size) i = 0; i < comm_size; ++i)
    //         {
    //             auto _cargv =
    //                 margv.at(i).get_execv({ pidexe, std::to_string(pids.at(i)) }, 1);
    //             cargv.push_back(_cargv);
    //             cargv_arg0.push_back(_cargv.argv()[0]);
    //             cargv_argv.push_back(_cargv.argv() + 1);
    //             if(debug() && comm_rank == 0)
    //             {
    //                 fprintf(stderr, "[%s][rank=%i]> cmd :: %s\n", command().c_str(),
    //                 (int) i,
    //                         _cargv.args().c_str());
    //             }
    //         }

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");
    //         tim::mpi::barrier(comm_world_v);

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");
    //         tim::mpi::comm_spawn_multiple(comm_size, cargv_arg0.data(),
    //         cargv_argv.data(),
    //                                       procs.data(), infos.data(), 0, comm_world_v,
    //                                       &comm_child_v, errcodes.data());
    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");

    //         // clean up
    //         for(auto& itr : cargv)
    //             itr.clear();
    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");
    //     }
    // #endif
    //     //
    //     //----------------------------------------------------------------------------------//
    //     TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");

    //     if(pid != 0)
    //     {
    //         if(use_mpi())
    //         {
    //             TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");
    //             if(!signal_delivered())
    //             {
    //                 // wait for timemory-pid to signal
    //                 pause();
    //             }
    //             worker_pid() = read_pid(master_pid());
    //         }
    //         else
    //         {
    //             TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");
    //             worker_pid() = pid;
    //         }

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");
    //         tim::process::get_target_id() = worker_pid();
    //         tim::settings::papi_attach()  = true;
    //         get_sampler()                 = new sampler_t{ compose_prefix() };
    //         if(use_sample() && !signal_types().empty())
    //         {
    //             get_measure()->set_buffer_size(buffer_size());
    //             buffer_thread() =
    //                 std::make_unique<std::thread>(&store_history, get_measure());
    //         }
    // #if defined(TIMEMORY_USE_PAPI)
    //         if(use_papi())
    //             papi_array_t::configure();
    // #endif
    //     }

    //     auto failed_fork = [&]() {
    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");
    //         // pid == -1 means error occured
    //         bool cond = (pid == -1);
    //         if(pid == -1)
    //             puts("failure forking, error occured!");
    //         return cond;
    //     };

    //     auto is_child = [&]() {
    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "");
    //         // pid == 0 means child process created if not using MPI
    //         bool cond = (pid == 0 && !use_mpi());
    //         return cond;
    //     };

    //     // exit code
    //     int ec = 0;

    //     if(failed_fork())
    //     {
    //         puts("Failure to fork");
    //         exit(EXIT_FAILURE);
    //     }
    //     else if(is_child())
    //     {
    //         if(!use_mpi())
    //         {
    //             TIMEMORY_CONDITIONAL_PRINT_HERE(
    //                 (debug() && verbose() > 1),
    //                 "waiting for signal to proceed (this=%i, parent=%i)", getpid(),
    //                 master_pid());

    //             int      _sig = 0;
    //             sigset_t _wait;
    //             sigemptyset(&_wait);
    //             sigaddset(&_wait, SIGINT);
    //             sigwait(&_wait, &_sig);

    //             TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1),
    //                                             "proceeding (this=%i, parent=%i)",
    //                                             getpid(), master_pid());
    //         }

    //         child_process(_argc, _argv);
    //     }
    //     else
    //     {
    //         // output file
    //         auto ofs = std::unique_ptr<std::ofstream>{};

    //         // ensure always disabled
    //         tim::settings::enabled() = true;

    //         if(!output_file().empty() && (debug() || verbose() > 1))
    //         {
    //             auto fname = get_config().get_output_filename({}, ".txt");
    //             ofs        = std::make_unique<std::ofstream>(fname.c_str());
    //         }

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
    //                                         "configuring sampler");

    //         for(auto itr : signal_types())
    //         {
    //             get_sampler()->configure(tim::sampling::timer{
    //                 itr, CLOCK_REALTIME, SIGEV_SIGNAL, sample_freq(), sample_delay()
    //                 });
    //         }

    //         if(!use_mpi())
    //         {
    //             TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1),
    //                                             "notifying worker (PID=%i) process is
    //                                             ready", worker_pid());
    //             kill(worker_pid(), SIGINT);
    //         }

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
    //                                         "starting sampler");
    //         get_sampler()->start();

    //         if((debug() && verbose() > 1) || verbose() > 2)
    //             std::cerr << "[AFTER START][" << pid << "]> " << *get_measure() <<
    //             std::endl;

    //         if(ofs)
    //         {
    //             TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
    //                                             "Setting output file");
    //             get_measure()->set_output(ofs.get());
    //         }

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "target pid =
    //         %i",
    //                                         (int) worker_pid());
    //         auto status = get_sampler()->wait(worker_pid(), verbose(), debug());

    //         if((debug() && verbose() > 1) || verbose() > 2)
    //             std::cerr << "[BEFORE STOP][" << pid << "]> " << *get_measure() <<
    //             std::endl;

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
    //                                         "stopping sampler");
    //         get_sampler()->stop();

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
    //                                         "ignoring signals");
    //         get_sampler()->ignore(signal_types());

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
    //         "barrier"); tim::mpi::barrier(comm_world_v);

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
    //         "processing"); parent_process(pid);

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s",
    //         "barrier"); tim::mpi::barrier(tim::mpi::comm_world_v);

    //         TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "exit code =
    //         %i",
    //                                         status);
    //         ec = status;
    //     }

    //     delete get_sampler();

    //     TIMEMORY_CONDITIONAL_PRINT_HERE((debug() && verbose() > 1), "%s", "Completed");
    //     if(use_mpi() || (!timem_mpi_was_finalized() && tim::dmp::size() == 1))
    //         tim::mpi::finalize();

    //     return ec;
}
