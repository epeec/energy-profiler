// sampler.hpp

#pragma once

#include "signaler.hpp"
#include "timed_sample.hpp"

#include <nrg/reader.hpp>
#include <nrg/error.hpp>
#include <nonstd/expected.hpp>

#include <atomic>
#include <future>
#include <vector>

namespace tep
{

    using timed_execution = std::vector<timed_sample>;
    using sampler_expected = nonstd::expected<timed_execution, std::error_code>;
    using sampler_promise = std::function<sampler_expected()>;

    // sampler_interface

    class sampler_interface
    {
    public:
        virtual ~sampler_interface() = default;
        virtual sampler_promise run()&;
        virtual sampler_expected run()&&;

    private:
        virtual sampler_expected results() = 0;
    };

    // null_sampler

    class null_sampler : public sampler_interface
    {
    public:
        null_sampler() = default;
        sampler_expected results() override;
    };

    // sampler

    class sampler : public sampler_interface
    {
    private:
        const nrgprf::reader* _reader;

    public:
        sampler(const nrgprf::reader*);

    protected:
        const nrgprf::reader* reader() const;
    };

    // short sampler

    class short_sampler : public sampler
    {
    private:
        timed_sample _start;
        timed_sample _end;

    public:
        using sampler::sampler;

        sampler_promise run() & override;
        sampler_expected run() && override;

    private:
        sampler_expected results() override;
    };

    // sync_sampler

    class sync_sampler : public sampler
    {
    public:
        sync_sampler(const nrgprf::reader*);

    private:
        sampler_expected results() override;

        virtual void work() const = 0;
    };

    // async_sampler

    class async_sampler : public sampler
    {
    private:
        std::future<sampler_expected> _future;

    public:
        async_sampler(const nrgprf::reader*);
        ~async_sampler();

        bool valid() const;

    protected:
        decltype(_future)& ftr();
        const decltype(_future)& ftr() const;
        void ftr(decltype(_future)&&);

        virtual sampler_expected async_work() = 0;
    };

    // null_async_sampler

    class null_async_sampler : public async_sampler
    {
    public:
        null_async_sampler();

    protected:
        sampler_expected async_work() override;

    private:
        sampler_expected results() override;

    };

    // sync_sampler_fn

    class sync_sampler_fn final : public sync_sampler
    {
    private:
        std::function<void()> _work;

    public:
        template<typename Callable>
        sync_sampler_fn(const nrgprf::reader* r, Callable&& work) :
            sync_sampler(r),
            _work(std::forward<Callable>(work))
        {}

    private:
        void work() const override;
    };

    // async_sampler_fn

    class async_sampler_fn final : public sampler_interface
    {
    private:
        std::unique_ptr<async_sampler> _sampler;
        std::function<void()> _work;

    public:
        template<typename Callable>
        async_sampler_fn(std::unique_ptr<async_sampler>&& as, Callable&& work) :
            _sampler(std::move(as)),
            _work(std::forward<Callable>(work))
        {}

    private:
        sampler_expected results() override;
    };


    class periodic_sampler : public async_sampler
    {
    private:
        signaler _sig;
        std::atomic_bool _finished;
        std::chrono::milliseconds _period;

    public:
        periodic_sampler(const nrgprf::reader*, const std::chrono::milliseconds& period);
        ~periodic_sampler();

        sampler_promise run() & override;
        sampler_expected run() && override;

        const std::chrono::milliseconds& period() const;

    protected:
        signaler& sig();
        const signaler& sig() const;

        bool finished() const;

    private:
        sampler_expected results() override;
    };


    class bounded_ps final : public periodic_sampler
    {
    public:
        static std::chrono::milliseconds default_period;

        bounded_ps(const nrgprf::reader*, const std::chrono::milliseconds& period = default_period);

    protected:
        sampler_expected async_work() override;
    };


    class unbounded_ps final : public periodic_sampler
    {
    private:
        size_t _initial_size;

    public:
        static std::chrono::milliseconds default_period;

        unbounded_ps(const nrgprf::reader*, size_t initial_size,
            const std::chrono::milliseconds& period = default_period);

    protected:
        sampler_expected async_work() override;
    };

}
