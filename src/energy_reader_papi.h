// energy_reader_papi.h
#pragma once

#include "energy_reader.h"

#include <array>
#include <vector>

#include <papi.h>

namespace tep
{

class energy_reader_papi : public energy_reader
{
private:
    struct sample_point
    {
        uint64_t number;
        std::vector<long long> values;

        sample_point(uint64_t count, size_t num_events);
    };

    struct event_data
    {
        enum class type
        {
            pkg_energy,
            dram_energy,
            none
        };
        type type;
        uint32_t socket;
        double multiplier;

        event_data(const std::string_view& name,
            const std::string_view& units);
    };

private:
    int _event_set;
    std::vector<sample_point> _samples;
    std::vector<event_data> _events;

public:
    energy_reader_papi();
    energy_reader_papi(energy_reader_papi&& other);
    ~energy_reader_papi();

    // disable copying
    energy_reader_papi(const energy_reader_papi& other) = delete;
    energy_reader_papi& operator=(const energy_reader_papi& other) = delete;

    virtual void start() override;
    virtual void sample() override;
    virtual void stop() override;

protected:
    virtual void print(std::ostream& os) const override;

private:
    void add_events(int cid);
};

}
