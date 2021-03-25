// config.hpp

#pragma once

#include <chrono>
#include <iosfwd>
#include <string>
#include <vector>


namespace cmmn
{
    template<typename R, typename E>
    class expected;
}

namespace tep
{

    // error handling

    enum class cfg_error_code
    {
        SUCCESS = 0,

        CONFIG_IO_ERROR,
        CONFIG_NOT_FOUND,
        CONFIG_OUT_OF_MEM,
        CONFIG_BAD_FORMAT,
        CONFIG_NO_CONFIG,

        INVALID_THREAD_CNT,

        SEC_LIST_EMPTY,
        SEC_NO_BOUNDS,
        SEC_NO_FREQ,
        SEC_INVALID_TARGET,
        SEC_INVALID_NAME,
        SEC_INVALID_EXTRA,
        SEC_INVALID_FREQ,
        SEC_INVALID_INTERVAL,
        SEC_INVALID_METHOD,
        SEC_INVALID_EXECS,
        SEC_INVALID_SAMPLES,
        SEC_INVALID_DURATION,

        PARAM_INVALID_DOMAIN_MASK,
        PARAM_INVALID_SOCKET_MASK,
        PARAM_INVALID_DEVICE_MASK,

        BOUNDS_NO_START,
        BOUNDS_NO_END,

        POS_NO_COMP_UNIT,
        POS_NO_LINE,
        POS_INVALID_COMP_UNIT,
        POS_INVALID_LINE
    };

    class cfg_error
    {
    private:
        cfg_error_code _code;

    public:
        cfg_error(cfg_error_code code) :
            _code(code)
        {}

        cfg_error_code code() const
        {
            return _code;
        }

        operator bool() const
        {
            return _code != cfg_error_code::SUCCESS;
        }
    };

    // structs

    class config_data
    {
    public:
        enum class profiling_method
        {
            energy_profile,
            energy_total
        };

        enum class target
        {
            cpu,
            gpu
        };

        class position
        {
        private:
            std::string _cu;
            uint32_t _line;

        public:
            position(const std::string& cu, uint32_t ln);
            position(std::string&& cu, uint32_t ln);
            position(const char* cu, uint32_t ln);

            const std::string& compilation_unit() const;
            uint32_t line() const;
        };

        class bounds
        {
        private:
            config_data::position _start;
            config_data::position _end;

        public:
            template<typename S, typename E>
            bounds(S&& s, E&& e);

            const config_data::position& start() const;
            const config_data::position& end() const;
        };

        class params
        {
        private:
            unsigned int _domain_mask;
            unsigned int _socket_mask;
            unsigned int _device_mask;

        public:
            params();
            params(unsigned int dommask, unsigned int sktmask, unsigned int devmask);

            unsigned int domain_mask() const;
            unsigned int socket_mask() const;
            unsigned int device_mask() const;
        };

        class section
        {
        private:
            std::string _name;
            std::string _extra;
            config_data::target _target;
            config_data::profiling_method _method;
            config_data::bounds _bounds;
            std::chrono::milliseconds _interval;
            uint32_t _executions;
            uint32_t _samples;

        public:
            template<typename N, typename E, typename B, typename I>
            section(N&& nm, E&& extr, config_data::target tgt, config_data::profiling_method mthd,
                B&& bnd, I&& intrv, uint32_t execs, uint32_t smp) :
                _name(std::forward<N>(nm)),
                _extra(std::forward<E>(extr)),
                _target(tgt),
                _method(mthd),
                _bounds(std::forward<B>(bnd)),
                _interval(std::forward<I>(intrv)),
                _executions(execs),
                _samples(smp)
            {}

            const std::string& name() const;
            const std::string& extra() const;

            config_data::target target() const;
            config_data::profiling_method method() const;
            const config_data::bounds& bounds() const;

            const std::chrono::milliseconds& interval() const;
            uint32_t executions() const;
            uint32_t samples() const;

            bool has_name() const;
            bool has_extra() const;
        };

    private:
        uint32_t _threads;
        config_data::params _parameters;
        std::vector<section> _sections;

    public:
        uint32_t threads() const;
        void threads(uint32_t t);

        void parameters(const config_data::params& p);
        const config_data::params& parameters() const;

        std::vector<section>& sections();
        const std::vector<section>& sections() const;
    };

    // operator overloads

    std::ostream& operator<<(std::ostream& os, const cfg_error& res);
    std::ostream& operator<<(std::ostream& os, const config_data::target& tgt);
    std::ostream& operator<<(std::ostream& os, const config_data::profiling_method& pm);
    std::ostream& operator<<(std::ostream& os, const config_data::params& p);
    std::ostream& operator<<(std::ostream& os, const config_data::position& p);
    std::ostream& operator<<(std::ostream& os, const config_data::bounds& b);
    std::ostream& operator<<(std::ostream& os, const config_data::section& s);
    std::ostream& operator<<(std::ostream& os, const config_data& cd);

    // types

    using cfg_result = cmmn::expected<config_data, cfg_error>;

    // functions

    cfg_result load_config(const char* file);
    cfg_result load_config(const std::string& file);

}
