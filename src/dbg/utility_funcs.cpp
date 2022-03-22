#include "utility_funcs.hpp"
#include "demangle.hpp"

#include <nonstd/expected.hpp>

#include <iostream>

namespace
{
    struct util_category_t : std::error_category
    {
        const char* name() const noexcept override
        {
            return "dbg-util";
        }

        std::string message(int ev) const override
        {
            using tep::dbg::util_errc;
            switch (static_cast<util_errc>(ev))
            {
            case util_errc::cu_not_found:
                return "Compilation unit not found";
            case util_errc::cu_ambiguous:
                return "Compilation unit ambiguous";
            case util_errc::file_not_found:
                return "File not found";
            case util_errc::line_not_found:
                return "Line not found";
            case util_errc::column_not_found:
                return "Column not found";
            case util_errc::symbol_not_found:
                return "Symbol not found";
            case util_errc::symbol_ambiguous:
                return "Symbol ambiguous";
            case util_errc::symbol_ambiguous_static:
                return "Symbol name ambiguous with at least one static symbol present";
            case util_errc::symbol_ambiguous_weak:
                return "Symbol name ambiguous with at least one weak symbol present";
            case util_errc::symbol_ambiguous_suffix:
                return "Symbol name ambiguous with at least one name with a suffix";
            case util_errc::no_matches:
                return "No matches found";
            case util_errc::function_not_found:
                return "Function not found";
            case util_errc::function_ambiguous:
                return "Function ambiguous";
            case util_errc::decl_location_not_found:
                return "No function with declaration location found";
            case util_errc::address_not_found:
                return "Address not found";
            }
            return "(unrecognized error code)";
        }

        std::error_condition default_error_condition(int ev) const noexcept override
        {
            using tep::dbg::util_errc;
            using tep::dbg::util_errcause;
            switch (static_cast<util_errc>(ev))
            {
            case util_errc::cu_not_found:
            case util_errc::file_not_found:
            case util_errc::line_not_found:
            case util_errc::column_not_found:
            case util_errc::symbol_not_found:
            case util_errc::no_matches:
            case util_errc::function_not_found:
            case util_errc::decl_location_not_found:
            case util_errc::address_not_found:
                return util_errcause::not_found;
            case util_errc::cu_ambiguous:
            case util_errc::symbol_ambiguous:
            case util_errc::symbol_ambiguous_static:
            case util_errc::symbol_ambiguous_weak:
            case util_errc::symbol_ambiguous_suffix:
            case util_errc::function_ambiguous:
                return util_errcause::ambiguous;
            }
            assert(false);
            return std::error_condition{};
        }
    };

    struct util_cause_category_t : std::error_category
    {
        const char* name() const noexcept override
        {
            return "dbg-util-cause";
        }

        std::string message(int ev) const override
        {
            using tep::dbg::util_errcause;
            switch (static_cast<util_errcause>(ev))
            {
            case util_errcause::not_found:
                return "Not found";
            case util_errcause::ambiguous:
                return "Ambiguous";
            case util_errcause::other:
                return "Other cause";
            }
            return "(unrecognized error cause)";
        }

        bool equivalent(const std::error_code& ec, int cv) const noexcept override
        {
            using tep::dbg::util_errcause;
            using tep::dbg::util_category;
            auto cond = static_cast<util_errcause>(cv);
            if (ec.category() == util_category())
                return cond == ec.default_error_condition();
            return cond == util_errcause::other;
        }
    };

    const util_category_t util_category_v;
    const util_cause_category_t util_cause_category_v;

    struct mangled_name_t {};
    struct demangled_name_t {};

    constexpr mangled_name_t mangled_name;
    constexpr demangled_name_t demangled_name;

    // check if sub is a subpath of path,
    // i.e. sub is an incomplete path of path
    bool is_sub_path(
        const std::filesystem::path& sub, const std::filesystem::path& path)
    {
        return !sub.empty() && (sub == path ||
            std::search(
                path.begin(), path.end(), sub.begin(), sub.end()) != path.end());
    }

    std::string remove_spaces(std::string_view str)
    {
        std::string ret(str);
        ret.erase(std::remove_if(ret.begin(), ret.end(), [](unsigned char c)
            {
                return std::isspace(c);
            }), ret.end());
        return ret;
    }

    template<typename Iter, typename IterAccess>
    tep::dbg::result<const tep::dbg::function_symbol*>
        find_function_symbol_exact_impl(
            IterAccess access,
            Iter first,
            Iter last,
            std::string_view name)
    {
        using tep::dbg::function_symbol;
        using tep::dbg::util_errc;
        using tep::dbg::symbol_binding;
        using unexpected = nonstd::unexpected<std::error_code>;

        std::error_code ec;
        auto pred = [&ec, name, access](const auto& sym)
        {
            auto demangled = tep::dbg::demangle(access(sym).name, ec);
            if (!demangled)
                return true;
            return remove_spaces(*demangled) == remove_spaces(name);
        };

        auto it = std::find_if(first, last, pred);
        if (ec)
            return unexpected{ ec };
        if (it == last)
            return unexpected{ util_errc::symbol_not_found };

        bool has_static = access(*it).binding == symbol_binding::local;
        bool has_weak = access(*it).binding == symbol_binding::weak;
        bool has_ambiguity = false;
        for (auto next_it = std::find_if(it + 1, last, pred);
            next_it != last && (has_ambiguity = true);
            next_it = std::find_if(next_it + 1, last, pred))
        {
            has_static = has_static ||
                access(*next_it).binding == symbol_binding::local;
            has_weak = has_weak ||
                access(*next_it).binding == symbol_binding::weak;
        }
        if (has_ambiguity)
        {
            if (has_weak)
                return unexpected{ util_errc::symbol_ambiguous_weak };
            if (has_static)
                return unexpected{ util_errc::symbol_ambiguous_static };
            return unexpected{ util_errc::symbol_ambiguous };
        }
        return &access(*it);
    }

    tep::dbg::result<const tep::dbg::function_symbol*>
        find_function_symbol_exact(
            const tep::dbg::object_info& oi,
            std::string_view name)
    {
        return find_function_symbol_exact_impl(
            [](auto& x) -> auto& { return x; },
            oi.function_symbols().begin(),
            oi.function_symbols().end(),
            name);
    }

    bool is_match_demangled(std::string_view to_match, std::string_view name)
    {
        std::string nospaces = remove_spaces(name);
        std::string nospaces_m = remove_spaces(to_match);
        return std::string_view{ nospaces }.substr(
            0, nospaces_m.size()) == nospaces_m;
    }

    tep::dbg::result<bool> is_match(
        std::string_view to_match, std::string_view name)
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        std::error_code ec;
        auto demangled = tep::dbg::demangle(name, ec);
        if (!demangled)
            return unexpected{ ec };
        return is_match_demangled(to_match, name);
    }

    tep::dbg::result<bool> is_equal(
        std::string_view name, std::string_view mangled)
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        std::error_code ec;
        auto demangled = tep::dbg::demangle(mangled, ec);
        if (!demangled)
            return unexpected{ ec };
        return remove_spaces(*demangled) == remove_spaces(name);
    }

    bool has_suffix(std::string_view x)
    {
        size_t pos = x.find('.');
        if (pos == std::string_view::npos)
            return false;
        return !x.substr(pos).empty();
    };

    tep::dbg::result<const tep::dbg::function_symbol*>
        find_function_symbol_matched(
            const tep::dbg::object_info& oi,
            std::string_view name,
            bool no_suffix)
    {
        using tep::dbg::function_symbol;
        using tep::dbg::util_errc;
        using tep::dbg::symbol_binding;
        using unexpected = nonstd::unexpected<std::error_code>;

        auto matches_pred = [name](const function_symbol& sym, std::error_code& ec)
        {
            auto is_match_res = is_match(name, sym.name);
            if (!is_match_res)
            {
                ec = is_match_res.error();
                return true;
            }
            return *is_match_res;
        };

        auto get_all_matches = [&](std::error_code& ec)
        {
            std::vector<const function_symbol*> matches;
            for (const auto& sym : oi.function_symbols())
            {
                bool is_match = matches_pred(sym, ec);
                if (ec)
                    break;
                if (is_match)
                    matches.push_back(&sym);
            }
            return matches;
        };

        static constexpr auto get_suffix = [](std::string_view x)
        {
            size_t pos = x.find('.');
            if (pos == std::string_view::npos)
                return std::string_view{};
            return x.substr(pos);
        };

        std::error_code ec;
        auto matches = get_all_matches(ec);
        if (ec)
            return unexpected{ ec };
        if (matches.empty())
            return unexpected{ util_errc::no_matches };
        if (matches.size() == 1)
            return matches.front();
        auto exact_match = find_function_symbol_exact_impl(
            [](auto& x) -> auto& { return *x; },
            matches.begin(),
            matches.end(),
            name);
        if (exact_match || exact_match.error() != util_errc::symbol_not_found)
            return exact_match;
        if (!no_suffix)
            return unexpected{ util_errc::symbol_ambiguous_suffix };
        auto it = std::partition(matches.begin(), matches.end(),
            [](const function_symbol* sym)
            {
                assert(sym);
                return !get_suffix(sym->name).empty();
            });
        if (it == matches.end())
            return unexpected{ util_errc::symbol_ambiguous_suffix };
        if (std::distance(it, matches.end()) > 1)
            return unexpected{ util_errc::symbol_ambiguous };
        return *it;
    }

    tep::dbg::result<const tep::dbg::function*>
        find_function_by_linkage_name(
            mangled_name_t,
            const tep::dbg::compilation_unit& cu,
            std::string_view name) noexcept
    {
        using tep::dbg::util_errc;
        using tep::dbg::function;
        using unexpected = nonstd::unexpected<std::error_code>;
        auto pred = [name](const function& af)
        {
            if (af.is_static())
                return false;
            return *af.linkage_name == name;
        };
        auto it = std::find_if(cu.funcs.begin(), cu.funcs.end(), pred);
        if (it == cu.funcs.end())
            return unexpected{ util_errc::function_not_found };
        return &*it;
    }

    tep::dbg::result<const tep::dbg::function*>
        find_function_by_linkage_name(
            demangled_name_t,
            const tep::dbg::compilation_unit& cu,
            std::string_view name,
            bool exact_name)
    {
        using tep::dbg::util_errc;
        using tep::dbg::function;
        using unexpected = nonstd::unexpected<std::error_code>;

        std::error_code ec;
        auto exact_pred = [&ec, name](const function& af)
        {
            if (af.is_static())
                return false;
            auto demangled = tep::dbg::demangle(*af.linkage_name, ec);
            if (!demangled)
                return true;
            return remove_spaces(*demangled) == remove_spaces(name);
        };

        if (exact_name)
        {
            auto it = std::find_if(cu.funcs.begin(), cu.funcs.end(), exact_pred);
            if (ec)
                return unexpected{ ec };
            if (it == cu.funcs.end())
                return unexpected{ util_errc::function_not_found };
            return &*it;
        }

        auto matches_pred = [name](const function& af, std::error_code& ec)
        {
            if (af.is_static())
                return false;
            auto is_match_res = is_match(name, *af.linkage_name);
            if (!is_match_res)
            {
                ec = is_match_res.error();
                return true;
            }
            return *is_match_res;
        };

        auto get_all_matches = [&](std::error_code& ec)
        {
            std::vector<const function*> matches;
            for (const auto& sym : cu.funcs)
            {
                bool is_match = matches_pred(sym, ec);
                if (ec)
                    break;
                if (is_match)
                    matches.push_back(&sym);
            }
            return matches;
        };

        auto matches = get_all_matches(ec);
        if (ec)
            return unexpected{ ec };
        if (matches.empty())
            return unexpected{ util_errc::no_matches };
        if (matches.size() == 1)
            return matches.front();
        auto it = std::find_if(matches.begin(), matches.end(),
            [exact_pred](const function* f)
            {
                assert(f);
                return exact_pred(*f);
            });
        if (ec)
            return unexpected{ ec };
        if (it == matches.end())
            return unexpected{ util_errc::function_ambiguous };
        return *it;
    }
}

namespace tep::dbg
{
    std::error_code make_error_code(util_errc x) noexcept
    {
        return { static_cast<int>(x), util_category() };
    }

    std::error_condition make_error_condition(util_errcause x) noexcept
    {
        return { static_cast<int>(x), util_cause_category_v };
    }

    const std::error_category& util_category() noexcept
    {
        return util_category_v;
    }

    result<const compilation_unit*>
        find_compilation_unit(
            const object_info& oi,
            const std::filesystem::path& cu) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        auto find = [&cu](auto first, auto last)
        {
            return std::find_if(first, last,
                [&cu](const compilation_unit& x)
                {
                    return is_sub_path(cu, x.path);
                });
        };

        auto end_it = oi.compilation_units().end();
        auto it = find(oi.compilation_units().begin(), end_it);
        if (it == end_it)
            return unexpected{ util_errc::cu_not_found };
        // find again from returned iterator to end to see if CU is ambiguous
        if (find(it + 1, end_it) != end_it)
            return unexpected{ util_errc::cu_ambiguous };
        return &*it;
    }

    result<const compilation_unit*>
        find_compilation_unit(
            const object_info& oi,
            const function_symbol& sym
        ) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        auto it = std::find_if(
            oi.compilation_units().begin(),
            oi.compilation_units().end(),
            [&](const compilation_unit& cu)
            {
                return cu.addresses.end() != std::find_if(
                    cu.addresses.begin(),
                    cu.addresses.end(),
                    [&](contiguous_range rng)
                    {
                        return sym.address >= rng.low_pc &&
                            sym.address < rng.high_pc;
                    });
            });
        if (it == oi.compilation_units().end())
            return unexpected{ util_errc::cu_not_found };
        return &*it;
    }

    result<std::pair<lines::const_iterator, lines::const_iterator>>
        find_lines(
            const compilation_unit& cu,
            const std::filesystem::path& file,
            uint32_t lineno,
            exact_line_value_flag exact_line,
            uint32_t colno,
            exact_column_value_flag exact_col) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (!lineno && colno)
            return unexpected{ make_error_code(std::errc::invalid_argument) };

        const auto& effective_file = file.empty() ? cu.path : file;

        static auto line_match = [](
            const source_line& line, uint32_t lineno, exact_line_value_flag exact_line)
        {
            return !lineno || (exact_line == exact_line_value_flag::yes ?
                line.number == lineno :
                line.number >= lineno);
        };

        static auto column_match = [](
            const source_line& line, uint32_t colno, exact_column_value_flag exact_col)
        {
            return !colno || (exact_col == exact_column_value_flag::yes ?
                line.column == colno :
                line.column >= colno);
        };

        bool file_found = false;
        auto start_it = std::find_if(cu.lines.begin(), cu.lines.end(),
            [&effective_file, &file_found, lineno, exact_line](const source_line& line)
            {
                return effective_file == line.file &&
                    (file_found = true) &&
                    line_match(line, lineno, exact_line);
            });
        if (start_it == cu.lines.end())
        {
            if (!file_found)
                return unexpected{ util_errc::file_not_found };
            return unexpected{ util_errc::line_not_found };
        }

        // if line advances with relation to the requested one
        // reset column to 0
        if (start_it->number > lineno && exact_col == exact_column_value_flag::no)
            colno = 0;

        start_it = std::find_if(start_it, cu.lines.end(),
            [&effective_file, lineno = start_it->number, colno, exact_col](
                const source_line& line)
        {
            return effective_file == line.file &&
                line_match(line, lineno, exact_line_value_flag::yes) &&
                column_match(line, colno, exact_col);
        });
        if (start_it == cu.lines.end())
            return unexpected{ util_errc::column_not_found };

        auto end_it = std::find_if_not(start_it, cu.lines.end(),
            [&effective_file, lineno = start_it->number](const source_line& line)
        {
            return effective_file == line.file &&
                line_match(line, lineno, exact_line_value_flag::yes);
        });

        end_it = std::find_if_not(end_it, cu.lines.end(),
            [&effective_file, lineno = start_it->number, colno = start_it->column](
                const source_line& line)
        {
            return effective_file == line.file &&
                line_match(line, lineno, exact_line_value_flag::yes) &&
                column_match(line, colno, exact_column_value_flag::yes);
        });
        assert(std::distance(start_it, end_it) > 0);
        return std::pair{ start_it, end_it };
    }

    result<const source_line*>
        find_line(
            const compilation_unit& cu,
            const source_location& loc
        ) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        auto lines = find_lines(cu, loc.file,
            loc.line_number, exact_line_value_flag::no,
            loc.line_column, exact_column_value_flag::no);
        if (!lines)
            return unexpected{ lines.error() };
        return lowest_address_line(lines->first, lines->second);
    }

    result<const source_line*> lowest_address_line(
        lines::const_iterator first,
        lines::const_iterator last,
        new_statement_flag new_stmt) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (new_stmt == new_statement_flag::no)
            return &*first;
        auto it = std::find_if(first, last, [](const source_line& line)
            {
                return line.new_statement;
            });
        if (it == last)
            return unexpected{ util_errc::line_not_found };
        return &*it;
    }

    result<const source_line*> highest_address_line(
        lines::const_iterator first,
        lines::const_iterator last,
        new_statement_flag new_stmt) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (new_stmt == new_statement_flag::no)
            return &*(first + std::distance(first, last) - 1);
        auto found = last;
        for (auto it = first; it != last; ++it)
            if (it->new_statement)
                found = it;
        if (found == last)
            return unexpected{ util_errc::line_not_found };
        return &*found;
    }

    result<const function_symbol*> find_function_symbol(
        const object_info& oi,
        std::string_view name,
        exact_symbol_name_flag exact_name,
        ignore_symbol_suffix_flag no_suffix)
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (name.empty())
            return unexpected{ make_error_code(std::errc::invalid_argument) };
        return exact_name == exact_symbol_name_flag::yes ?
            find_function_symbol_exact(oi, name) :
            find_function_symbol_matched(oi, name,
                no_suffix == ignore_symbol_suffix_flag::yes);
    }

    result<const function_symbol*>
        find_function_symbol(
            const object_info& oi,
            const compilation_unit& cu,
            std::string_view name,
            exact_symbol_name_flag exact_name,
            ignore_symbol_suffix_flag no_suffix)
    {
        using ret_type = result<const function_symbol*>;
        using unexpected = nonstd::unexpected<std::error_code>;
        if (name.empty())
            return unexpected{ make_error_code(std::errc::invalid_argument) };

        auto find_exact = [&]() -> ret_type
        {
            for (const auto& sym : oi.function_symbols())
            {
                if (auto res = is_equal(name, sym.name); !res)
                    return unexpected{ res.error() };
                else if (!*res)
                    continue;
                auto cu_res = find_compilation_unit(oi, sym);
                if (cu_res && (*cu_res)->path == cu.path)
                    return &sym;
            }
            return unexpected{ util_errc::symbol_not_found };
        };

        auto ambiguous_error = [](symbol_binding b1, symbol_binding b2)
            -> std::error_code
        {
            if (b1 == symbol_binding::weak || b2 == symbol_binding::weak)
                return util_errc::symbol_ambiguous_weak;
            if (b1 == symbol_binding::local || b2 == symbol_binding::local)
                return util_errc::symbol_ambiguous_static;
            return util_errc::symbol_ambiguous;
        };

        auto find_matched = [&](bool no_suffix) -> ret_type
        {
            const function_symbol* found = nullptr;
            for (const auto& sym : oi.function_symbols())
            {
                if (auto res = is_match(name, sym.name); !res)
                    return unexpected{ res.error() };
                else if (!*res)
                    continue;
                auto cu_res = find_compilation_unit(oi, sym);
                if (!cu_res || (*cu_res)->path != cu.path)
                    continue;
                if (auto res = is_equal(name, sym.name); !res)
                    return unexpected{ res.error() };
                else if (*res)
                    return &sym;
                if (!found)
                    found = &sym;
                else if (!no_suffix)
                {
                    if (has_suffix(sym.name) || has_suffix(found->name))
                        return unexpected{ util_errc::symbol_ambiguous_suffix };
                    return unexpected{ ambiguous_error(sym.binding, found->binding) };
                }
                else
                {
                    if (!has_suffix(sym.name))
                        found = &sym;
                    else
                    {
                        if (has_suffix(found->name))
                        {
                            if (has_suffix(sym.name))
                                return unexpected{ util_errc::symbol_ambiguous_suffix };
                            return unexpected{ ambiguous_error(sym.binding, found->binding) };
                        }
                    }
                }
            }
            if (found)
                return found;
            return unexpected{ util_errc::symbol_not_found };
        };

        if (bool(exact_name))
            return find_exact();
        return find_matched(bool(no_suffix));
    }

    result<const function_symbol*>
        find_function_symbol(
            const object_info& oi,
            uintptr_t addr) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        auto it = std::find_if(
            oi.function_symbols().begin(),
            oi.function_symbols().end(),
            [addr](const function_symbol& sym)
            {
                return sym.address == addr;
            });
        if (it == oi.function_symbols().end())
            return unexpected{ util_errc::address_not_found };
        return &*it;
    }

    result<const function_symbol*>
        find_function_symbol(
            const object_info& oi,
            const function& f
        ) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        const auto& syms = oi.function_symbols();
        if (f.is_extern())
        {
            auto it = std::find_if(syms.begin(), syms.end(), [&f](const function_symbol& sym)
                {
                    return sym.name == *f.linkage_name;
                });
            if (it == syms.end())
                return unexpected{ util_errc::symbol_not_found };
            return &*it;
        }
        if (!f.addresses)
            return unexpected{ util_errc::symbol_not_found };
        if (f.addresses->values.size() > 1)
            return unexpected{ util_errc::symbol_ambiguous };
        assert(!f.addresses->values.empty());
        auto it = std::find_if(syms.begin(), syms.end(), [&f](const function_symbol& sym)
            {
                const auto& addrs = f.addresses->values;
                return sym.address == addrs.front().low_pc;
            });
        if (it == syms.end())
            return unexpected{ util_errc::symbol_not_found };
        return &*it;
    }

    result<const function*>
        find_function(
            const compilation_unit& cu,
            const function_symbol& f)
        noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        // static function, lookup using address
        // otherwise, lookup using linkage name
        if (f.binding == symbol_binding::local)
        {
            auto it = std::find_if(cu.funcs.begin(), cu.funcs.end(),
                [sym_addr = f.address](const function& f)
            {
                if (!f.is_static())
                    return false;
                if (!f.addresses)
                    return false;
                // multiple ranges but not inlined
                const auto& rngs = f.addresses->values;
                return rngs.end() != std::find_if(rngs.begin(), rngs.end(),
                    [sym_addr](const contiguous_range& rng)
                    {
                        return rng.low_pc == sym_addr;
                    });
            });
            if (it == cu.funcs.end())
                return unexpected{ util_errc::function_not_found };
            return &*it;
        }
        return find_function_by_linkage_name(mangled_name, cu, f.name);
    }

    result<const function*>
        find_function(
            const object_info& oi,
            const function_symbol& f
        ) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        for (const auto& cu : oi.compilation_units())
        {
            auto func = find_function(cu, f);
            if (func || func.error() != util_errc::function_not_found)
                return func;
        }
        return unexpected{ util_errc::function_not_found };
    }

    result<std::pair<const function*, const function_symbol*>>
        find_function(
            const object_info& oi,
            std::string_view name,
            exact_symbol_name_flag exact_name)
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        auto sym = find_function_symbol(oi, name,
            exact_name, ignore_symbol_suffix_flag::yes);
        if (sym)
        {
            if (auto res = find_function(oi, **sym))
                return std::pair{ *res, *sym };
            else
                return unexpected{ res.error() };
        }
        else if (sym.error() == util_errcause::not_found)
        {
            for (const auto& cu : oi.compilation_units())
            {
                auto func = find_function_by_linkage_name(
                    demangled_name, cu, name, exact_name == exact_symbol_name_flag::yes);
                if (func)
                    return std::pair{ *func, nullptr };
                if (func.error() != util_errc::function_not_found)
                    return unexpected{ func.error() };
            }
            return unexpected{ util_errc::function_not_found };
        }
        return unexpected{ sym.error() };
    }

    result<std::pair<const function*, const function_symbol*>>
        find_function(
            const object_info& oi,
            const compilation_unit& cu,
            std::string_view name,
            exact_symbol_name_flag exact_name)
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        auto sym = find_function_symbol(oi, cu, name,
            exact_name, ignore_symbol_suffix_flag::yes);
        if (!sym && sym.error() != util_errcause::not_found)
            return unexpected{ sym.error() };
        if (!sym)
        {
            if (auto res = find_function(oi, **sym))
                return std::pair{ *res, nullptr };
            else
                return unexpected{ res.error() };
        }
        for (const auto& f : cu.funcs)
        {
            if (f.is_extern() && *f.linkage_name == (*sym)->name)
                return std::pair{ &f, *sym };
            if (f.is_static() && f.addresses)
            {
                auto it = std::find_if(
                    f.addresses->values.begin(), f.addresses->values.end(),
                    [&](contiguous_range rng)
                    {
                        return rng.low_pc == (*sym)->address;
                    });
                if (it != f.addresses->values.end())
                    return std::pair{ &f, *sym };
            }
        }
        return unexpected{ util_errc::function_not_found };
    }

    result<const function*>
        find_function(
            const compilation_unit& cu,
            std::string_view name,
            exact_symbol_name_flag exact_name)
    {
        using unexpected = result<const function*>::unexpected_type;
        const function* found = nullptr;
        // if symbol is not found we can check by linkage name
        // only if the function is extern
        // if it is a static function, then assume it was not
        // inlined and symbol has been found
        // TODO: maybe possible to still have symbol
        // but direct calls be inlined ? (e.g. function pointers)
        for (const auto& f : cu.funcs)
        {
            if (f.is_static())
                continue;
            std::error_code ec;
            auto demangled = demangle(*f.linkage_name, ec);
            if (!demangled)
                return unexpected{ ec };
            if (bool(exact_name))
            {
                if (remove_spaces(*demangled) == remove_spaces(name))
                    return &f;
            }
            else if (is_match_demangled(name, *demangled))
            {
                if (found)
                    return unexpected{ util_errc::function_ambiguous };
                found = &f;
            }
        }
        if (found)
            return found;
        return unexpected{ util_errc::function_not_found };
    }

    result<std::pair<functions::const_iterator, functions::const_iterator>>
        find_functions(
            const compilation_unit& cu,
            const std::filesystem::path& file)
        noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;

        auto pred = [&file](const function& f)
        {
            return f.decl_loc && f.decl_loc->file == file;
        };

        auto start_it = std::find_if(cu.funcs.begin(), cu.funcs.end(), pred);
        if (start_it == cu.funcs.end())
            return unexpected{ util_errc::file_not_found };
        auto end_it = std::find_if_not(start_it + 1, cu.funcs.end(), pred);
        assert(std::distance(start_it, end_it) > 0);
        return std::pair{ start_it, end_it };
    }

    result<const function*>
        find_function(
            const compilation_unit& cu,
            const std::filesystem::path& file,
            uint32_t lineno,
            uint32_t colno)
        noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        bool file_found{}, line_found{}, col_found{}, decl_loc_found{};
        auto pred = [&](const function& f)
        {
            return f.decl_loc && (decl_loc_found = true) &&
                f.decl_loc->file == file && (file_found = true) &&
                f.decl_loc->line_number == lineno && (line_found = true) &&
                (!colno || (f.decl_loc->line_column == colno && (col_found = true)));
        };

        auto it = std::find_if(cu.funcs.begin(), cu.funcs.end(), pred);
        if (it == cu.funcs.end())
        {
            auto ec = util_errc::function_not_found;
            if (!decl_loc_found)
                ec = util_errc::decl_location_not_found;
            else if (!file_found)
                ec = util_errc::file_not_found;
            else if (!line_found)
                ec = util_errc::line_not_found;
            else if (!col_found)
                ec = util_errc::column_not_found;
            return unexpected{ ec };
        }

        if (std::find_if(it + 1, cu.funcs.end(), pred) != cu.funcs.end())
            return unexpected{ util_errc::function_ambiguous };
        return &*it;
    }
} // namespace tep::dbg
