// config.cpp

#include "config.hpp"

#include <iostream>
#include <cstring>

#include <pugixml.hpp>
#include <expected.hpp>

using namespace tep;

template<typename R>
using cfg_expected = cmmn::expected<R, cfg_error>;

static const char* error_messages[] =
{
    "No error",

    "I/O error when loading config file",
    "Config file not found",
    "Out of memory when loading config file",
    "Config file is badly formatted",
    "Node <config></config> not found",

    "Invalid thread count in <threads></threads>",

    "Section list <sections></sections> is empty",
    "section: Node <bounds></bounds> not found",
    "section: Node <freq></freq> not found",
    "section: target must be 'cpu' or 'gpu'",
    "section: name cannot be empty",
    "section: extra data cannot be empty",
    "section: frequency must be a positive decimal number",
    "section: interval must be a positive integer",
    "section: method must be 'profile' or 'total'",
    "section: executions must be a positive integer",
    "section: samples must be a positive integer",
    "section: duration must be a positive integer",

    "params: parameter 'domain_mask' must be a valid integer",
    "params: parameter 'socket_mask' must be a valid integer",
    "params: parameter 'device_mask' must be a valid integer",

    "bounds: node <start></start> not found",
    "bounds: node <end></end> not found",

    "Node <cu></cu> not found",
    "Node <line></line> not found",
    "Invalid compilation unit: cannot be empty",
    "Invalid line number: must be a positive integer"
};


// begin helper functions

cfg_expected<config_data::target> get_target(const pugi::xml_node& nsection)
{
    using namespace pugi;
    xml_attribute tgt_attr = nsection.attribute("target");
    if (!tgt_attr)
        return config_data::target::cpu;
    const char_t* tgt_str = tgt_attr.value();
    if (!strcmp(tgt_str, "cpu"))
        return config_data::target::cpu;
    if (!strcmp(tgt_str, "gpu"))
        return config_data::target::gpu;
    return cfg_error(cfg_error_code::SEC_INVALID_TARGET);
}

cfg_expected<config_data::params> get_params(const pugi::xml_node& nparams)
{
    using namespace pugi;
    // all domains, devices and sockets are considered by default
    unsigned int dommask = ~0x0;
    unsigned int sktmask = ~0x0;
    unsigned int devmask = ~0x0;
    xml_node ndomains = nparams.child("domain_mask");
    // <domain_mask></domain_mask> exists
    if (ndomains)
        // <domain_mask></domain_mask> must be a valid, positive integer
        if (!(dommask = ndomains.text().as_uint(0)))
            return cfg_error(cfg_error_code::PARAM_INVALID_DOMAIN_MASK);
    xml_node nsockets = nparams.child("socket_mask");
    // <socket_mask></socket_mask> exists
    if (nsockets)
        // <socket_mask></socket_mask> must be a valid, positive integer
        if (!(sktmask = nsockets.text().as_uint(0)))
            return cfg_error(cfg_error_code::PARAM_INVALID_SOCKET_MASK);
    xml_node ndevs = nparams.child("device_mask");
    // <device_mask></device_mask> exists
    if (ndevs)
        // <device_mask></device_mask> must be a valid, positive integer
        if (!(devmask = ndevs.text().as_uint(0)))
            return cfg_error(cfg_error_code::PARAM_INVALID_DEVICE_MASK);
    return { dommask, sktmask, devmask };
}

cfg_expected<std::chrono::milliseconds> get_interval(const pugi::xml_node& nsection)
{
    using namespace pugi;
    xml_node nfreq = nsection.child("freq");
    xml_node nint = nsection.child("interval");
    // <interval></interval> overrides <freq></freq>
    if (nint)
    {
        // <interval></interval> must be a valid, positive integer
        int interval = nint.text().as_int(0);
        if (interval <= 0)
            return cfg_error(cfg_error_code::SEC_INVALID_INTERVAL);
        return interval;
    }
    if (nfreq)
    {
        // <freq></freq> must be a positive decimal number
        double freq = nfreq.text().as_double(0.0);
        if (freq <= 0.0)
            return cfg_error(cfg_error_code::SEC_INVALID_FREQ);
        // clamps at 1000 Hz
        double interval = 1000.0 / freq;
        return interval <= 1.0 ? 1 : static_cast<unsigned int>(interval);
    }
    return cfg_error(cfg_error_code::SEC_NO_FREQ);
}

cfg_expected<uint32_t> get_samples(const pugi::xml_node& nsection, const std::chrono::milliseconds& interval)
{
    using namespace pugi;
    xml_node nsamp = nsection.child("samples");
    xml_node ndur = nsection.child("duration");
    if (ndur)
    {
        int duration = ndur.text().as_int(0);
        if (duration <= 0)
            return cfg_error(cfg_error_code::SEC_INVALID_DURATION);
        return duration / interval.count() + (duration % interval.count() != 0);
    }
    if (nsamp)
    {
        int samples = nsamp.text().as_uint(0);
        if (samples <= 0)
            return cfg_error(cfg_error_code::SEC_INVALID_SAMPLES);
        return samples;
    }
    // return default value
    return 0;
}

cfg_expected<config_data::position> get_position(const pugi::xml_node& pos_node)
{
    using namespace pugi;
    xml_node cu = pos_node.child("cu");
    // <cu></cu> exists
    if (!cu)
        return cfg_error(cfg_error_code::POS_NO_COMP_UNIT);
    // <cu></cu> is not empty
    if (!*cu.child_value())
        return cfg_error(cfg_error_code::POS_INVALID_COMP_UNIT);
    xml_node line = pos_node.child("line");
    // <line></line> exists
    if (!line)
        return cfg_error(cfg_error_code::POS_NO_LINE);
    // <line></line> is not empty or negative
    int lineno;
    if ((lineno = line.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::POS_INVALID_LINE);
    return { cu.child_value(), lineno };
}

cfg_expected<config_data::bounds> get_bounds(const pugi::xml_node& bounds)
{
    using namespace pugi;
    // <start></start>
    xml_node start = bounds.child("start");
    if (!start)
        return cfg_error(cfg_error_code::BOUNDS_NO_START);
    // <end></end>
    xml_node end = bounds.child("end");
    if (!end)
        return cfg_error(cfg_error_code::BOUNDS_NO_END);

    // position error checks
    cfg_expected<config_data::position> pstart = get_position(start);
    if (!pstart)
        return std::move(pstart.error());
    cfg_expected<config_data::position> pend = get_position(end);
    if (!pend)
        return std::move(pend.error());

    return { std::move(pstart.value()), std::move(pend.value()) };
}

cfg_expected<config_data::profiling_method> get_method(const pugi::xml_node& method)
{
    const pugi::char_t* method_str = method.child_value();
    if (!strcmp(method_str, "profile"))
        return config_data::profiling_method::energy_profile;
    if (!strcmp(method_str, "total"))
        return config_data::profiling_method::energy_total;
    return cfg_error(cfg_error_code::SEC_INVALID_METHOD);
}

cfg_expected<config_data::section> get_section(const pugi::xml_node& nsection)
{
    using namespace pugi;
    // attribute target
    cfg_expected<config_data::target> target = get_target(nsection);
    if (!target)
        return std::move(target.error());

    // <name></name> - optional, must not be empty
    xml_node nname = nsection.child("name");
    if (nname && !*nname.child_value())
        return cfg_error(cfg_error_code::SEC_INVALID_NAME);
    // <extra></extra> - optional, must not be empty
    xml_node nxtra = nsection.child("extra");
    if (nxtra && !*nxtra.child_value())
        return cfg_error(cfg_error_code::SEC_INVALID_EXTRA);

    // get interval
    cfg_expected<std::chrono::milliseconds> interval = get_interval(nsection);
    if (!interval)
        return std::move(interval.error());

    // <method></method> - optional
    // default is 'total'; should have no effect when target is 'gpu' due to the
    // nature of the power/energy reading interface
    xml_node nmethod = nsection.child("method");
    config_data::profiling_method method = config_data::profiling_method::energy_profile;
    if (nmethod)
    {
        cfg_expected<config_data::profiling_method> result = get_method(nmethod);
        if (!result)
            return std::move(result.error());
        if (target.value() == config_data::target::cpu)
            method = result.value();
    }

    // <execs></execs> - optional, must be a positive integer
    // if not present - use default value of 0
    xml_node nexecs = nsection.child("execs");
    int execs = 0;
    if (nexecs && (execs = nexecs.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::SEC_INVALID_EXECS);

    // get samples
    cfg_expected<uint32_t> samples = get_samples(nsection, interval.value());

    // <bounds></bounds>
    xml_node nbounds = nsection.child("bounds");
    if (!nbounds)
        return cfg_error(cfg_error_code::SEC_NO_BOUNDS);
    cfg_expected<config_data::bounds> bounds = get_bounds(nbounds);
    if (!bounds)
        return std::move(bounds.error());

    return {
        nname.child_value(),
        nxtra.child_value(),
        target.value(),
        method,
        std::move(bounds.value()),
        std::move(interval.value()),
        execs,
        samples.value() };
}

// end helper functions


// position

config_data::position::position(const std::string& cu, uint32_t ln) :
    _cu(cu),
    _line(ln)
{}

config_data::position::position(std::string&& cu, uint32_t ln) :
    _cu(std::move(cu)),
    _line(ln)
{}

config_data::position::position(const char* cu, uint32_t ln) :
    _cu(cu),
    _line(ln)
{}

const std::string& config_data::position::compilation_unit() const
{
    return _cu;
}

uint32_t config_data::position::line() const
{
    return _line;
}


// bounds

template<typename S, typename E>
config_data::bounds::bounds(S&& s, E&& e) :
    _start(std::forward<S>(s)),
    _end(std::forward<E>(e))
{}

template
config_data::bounds::bounds(const config_data::position&, const config_data::position&);

template
config_data::bounds::bounds(const config_data::position&, config_data::position&&);

template
config_data::bounds::bounds(config_data::position&&, const config_data::position&);

template
config_data::bounds::bounds(config_data::position&&, config_data::position&&);

const config_data::position& config_data::bounds::start() const
{
    return _start;
}

const config_data::position& config_data::bounds::end() const
{
    return _end;
}


// params

config_data::params::params() :
    params(~0x0, ~0x0, ~0x0)
{}

config_data::params::params(unsigned int dommask, unsigned int sktmask, unsigned int devmask) :
    _domain_mask(dommask),
    _socket_mask(sktmask),
    _device_mask(devmask)
{}

unsigned int config_data::params::domain_mask() const
{
    return _domain_mask;
}

unsigned int config_data::params::socket_mask() const
{
    return _socket_mask;
}

unsigned int config_data::params::device_mask() const
{
    return _device_mask;
}


// section

const std::string& config_data::section::name() const
{
    return _name;
}

const std::string& config_data::section::extra() const
{
    return _extra;
}

config_data::target config_data::section::target() const
{
    return _target;
}

config_data::profiling_method config_data::section::method() const
{
    return _method;
}

const config_data::bounds& config_data::section::bounds() const
{
    return _bounds;
}

const std::chrono::milliseconds& config_data::section::interval() const
{
    return _interval;
}

uint32_t config_data::section::executions() const
{
    return _executions;
}

uint32_t config_data::section::samples() const
{
    return _samples;
}

bool config_data::section::has_name() const
{
    return !_name.empty();
}

bool config_data::section::has_extra() const
{
    return !_extra.empty();
}


// config_data

uint32_t config_data::threads() const
{
    return _threads;
}

void config_data::threads(uint32_t t)
{
    _threads = t;
}

void config_data::parameters(const config_data::params& p)
{
    _parameters = p;
}

const config_data::params& config_data::parameters() const
{
    return _parameters;
}

std::vector<config_data::section>& config_data::sections()
{
    return _sections;
}

const std::vector<config_data::section>& config_data::sections() const
{
    return _sections;
}


// load_config

cfg_expected<config_data> tep::load_config(const char* file)
{
    using namespace pugi;

    config_data cfgdata;
    xml_document doc;
    xml_parse_result parse_result = doc.load_file(file);
    if (!parse_result)
    {
        switch (parse_result.status)
        {
        case status_file_not_found:
            return cfg_error(cfg_error_code::CONFIG_NOT_FOUND);
        case status_io_error:
            return cfg_error(cfg_error_code::CONFIG_IO_ERROR);
        case status_out_of_memory:
            return cfg_error(cfg_error_code::CONFIG_OUT_OF_MEM);
        default:
            return cfg_error(cfg_error_code::CONFIG_BAD_FORMAT);
        }
    }
    // <config></config>
    xml_node nconfig = doc.child("config");
    if (!nconfig)
        return cfg_error(cfg_error_code::CONFIG_NO_CONFIG);

    // <threads></threads> - optional, must be positive integer
    // default value of 0
    xml_node nthreads = nconfig.child("threads");
    int threads = 0;
    if (nthreads && (threads = nthreads.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::INVALID_THREAD_CNT);

    // <params></params> - optional, use default values if does not exist
    xml_node nparams = nconfig.child("params");
    if (nparams)
    {
        cfg_expected<config_data::params> custom_params = get_params(nparams);
        if (!custom_params)
            return std::move(custom_params.error());
        cfgdata.parameters(custom_params.value());
    }

    // iterate all sections
    // <sections></sections> - optional
    xml_node nsections = nconfig.child("sections");
    if (nsections)
    {
        int sec_count = 0;
        for (xml_node nsection = nsections.first_child(); nsection; nsection = nsection.next_sibling(), sec_count++)
        {
            // <section></section>
            cfg_expected<config_data::section> section = get_section(nsection);
            if (!section)
                return std::move(section.error());
            cfgdata.sections().push_back(std::move(section.value()));
        }
        if (sec_count == 0)
            return cfg_error(cfg_error_code::SEC_LIST_EMPTY);
    }

    cfgdata.threads(threads);
    return cfgdata;
}

cfg_result tep::load_config(const std::string& file)
{
    return load_config(file.c_str());
}

// operator overloads

std::ostream& tep::operator<<(std::ostream& os, const cfg_error& res)
{
    auto idx = static_cast<std::underlying_type_t<cfg_error_code>>(res.code());
    os << error_messages[idx] << " (error code " << idx << ")";
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::target& tgt)
{
    switch (tgt)
    {
    case config_data::target::cpu:
        os << "cpu";
        break;
    case config_data::target::gpu:
        os << "gpu";
        break;
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::params& p)
{
    std::ios::fmtflags flags(os.flags());
    os << "domains: " << "0x" << std::hex << p.domain_mask();
    os << "\nsockets: " << "0x" << p.socket_mask();
    os << "\ndevices: " << "0x" << p.device_mask();
    os.flags(flags);
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::profiling_method& pm)
{
    switch (pm)
    {
    case config_data::profiling_method::energy_profile:
        os << "profile";
        break;
    case config_data::profiling_method::energy_total:
        os << "total energy consumption";
        break;
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::position& p)
{
    os << p.compilation_unit() << ":" << p.line();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::bounds& s)
{
    os << s.start() << " - " << s.end();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::section& s)
{
    os << "name: " << (s.has_name() ? s.name() : "-");
    os << "\nextra: " << (s.has_extra() ? s.extra() : "-");
    os << "\ntarget: " << s.target();
    os << "\ninterval: " << s.interval().count() << " ms";
    os << "\nmethod: " << s.method();
    os << "\nbounds: " << s.bounds();
    os << "\nexecutions: " << s.executions();
    os << "\nsamples: " << s.samples();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data& cd)
{
    os << "threads: " << cd.threads();
    os << "\n" << cd.parameters();
    os << "\nsections:";
    for (const auto& section : cd.sections())
        os << "\n----------\n" << section;
    return os;
}
