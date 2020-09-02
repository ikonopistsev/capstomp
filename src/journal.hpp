#pragma once

namespace capstomp {

class journal
{
    int mask_{};

public:
    journal() noexcept;

    ~journal() noexcept;

    template<class F>
    void cerr(F fn) const noexcept
    {
        try
        {
            auto level = error_level();
            if (level_allow(level))
                output(level, fn());
        }
        catch (...)
        {   }
    }

    template<class F>
    void cout(F fn) const noexcept
    {
        try
        {
            auto level = notice_level();
            if (level_allow(level))
                output(level, fn());
        }
        catch (...)
        {   }
    }

private:
    void output(int level, const char *str) const noexcept;

    template<class T>
    void output(int level, const T& text) const noexcept
    {
        output(level, text.c_str());
    }

    int error_level() const noexcept;

    int notice_level() const noexcept;

    bool level_allow(int level) const noexcept;
};

} // namespace captor
