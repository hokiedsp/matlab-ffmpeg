#pragma once

#include <vector>
#include <unordered_map>
#include <functional>
#include <experimental/type_traits>

template <class...>
struct type_list
{
};

template <class... TYPES>
struct visitor_base
{
    using types = type_list<TYPES...>;
};

struct stream_container
{
public:
    stream_container() = default;
    stream_container(const stream_container &_other)
    {
        *this = _other;
    }

    stream_container &operator=(const stream_container &_other)
    {
        clear();
        clear_functions = _other.clear_functions;
        copy_functions = _other.copy_functions;
        size_functions = _other.size_functions;
        for (auto &&copy_function : copy_functions)
        {
            copy_function(_other, *this);
        }
        return *this;
    }

    template <class T>
    void push_back(const T &_t)
    {
        // don't have it yet, so create functions for printing, copying, moving, and destroying
        if (items<T>.find(this) == std::end(items<T>))
        {
            clear_functions.emplace_back([](stream_container &_c) { items<T>.erase(&_c); });

            // if someone copies me, they need to call each copy_function and pass themself
            copy_functions.emplace_back([](const stream_container &_from, stream_container &_to) {
                items<T>[&_to] = items<T>[&_from];
            });
            size_functions.emplace_back([](const stream_container &_c) { return items<T>[&_c].size(); });
        }
        items<T>[this].push_back(_t);
    }

    void clear()
    {
        for (auto &&clear_func : clear_functions)
        {
            clear_func(*this);
        }
    }

    template <class T>
    size_t number_of() const
    {
        auto iter = items<T>.find(this);
        if (iter != items<T>.cend())
            return items<T>[this].size();
        return 0;
    }

    size_t size() const
    {
        size_t sum = 0;
        for (auto &&size_func : size_functions)
        {
            sum += size_func(*this);
        }
        // gotta be careful about this overflowing
        return sum;
    }

    ~stream_container()
    {
        clear();
    }

    template <class T>
    void visit(T &&visitor)
    {
        visit_impl(visitor, typename std::decay_t<T>::types{});
    }

private:
    template <class T>
    static std::unordered_map<const stream_container *, std::vector<T>> items;

    template <class T, class U>
    using visit_function = decltype(std::declval<T>().operator()(std::declval<U &>()));

    // template <class T, class U>
    // static constexpr bool has_visit_v = std::experimental::is_detected<visit_function, T, U>::value;

    template <class T, template <class...> class TLIST, class... TYPES>
    void visit_impl(T &&visitor, TLIST<TYPES...>)
    {
        (..., visit_impl_help<std::decay_t<T>, TYPES>(visitor));
    }

    template <class T, class U>
    void visit_impl_help(T &visitor)
    {
        // static_assert(has_visit_v<T, U>, "Visitors must provide a visit function accepting a reference for each type");
        for (auto &&element : items<U>[this])
        {
            visitor(element);
        }
    }

    std::vector<std::function<void(stream_container &)>> clear_functions;
    std::vector<std::function<void(const stream_container &, stream_container &)>> copy_functions;
    std::vector<std::function<size_t(const stream_container &)>> size_functions;
};

template <class T>
std::unordered_map<const stream_container *, std::vector<T>> stream_container::items;
