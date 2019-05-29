#include <vector>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <experimental/type_traits>

namespace andyg{
template<class...>
struct type_list{};
  
template<class... TYPES>
struct visitor_base
{
    using types = andyg::type_list<TYPES...>;
};
        
struct heterogeneous_container
{
    public:
    heterogeneous_container() = default;
    heterogeneous_container(const heterogeneous_container& _other)
    {
        *this = _other;
    }
    
    heterogeneous_container& operator=(const heterogeneous_container& _other)
    {
        clear();
        clear_functions = _other.clear_functions;
        copy_functions = _other.copy_functions;
        size_functions = _other.size_functions;
        for (auto&& copy_function : copy_functions)
        {
            copy_function(_other, *this);
        }
        return *this;
    }
    
    template<class T>
    void push_back(const T& _t)
    {
        // don't have it yet, so create functions for printing, copying, moving, and destroying
        if (items<T>.find(this) == std::end(items<T>))
        {   
            clear_functions.emplace_back([](heterogeneous_container& _c){items<T>.erase(&_c);});
            
            // if someone copies me, they need to call each copy_function and pass themself
            copy_functions.emplace_back([](const heterogeneous_container& _from, heterogeneous_container& _to)
                                        {
                                            items<T>[&_to] = items<T>[&_from];
                                        });
            size_functions.emplace_back([](const heterogeneous_container& _c){return items<T>[&_c].size();});
        }
        items<T>[this].push_back(_t);
    }
    
    void clear()
    {
        for (auto&& clear_func : clear_functions)
        {
            clear_func(*this);
        }
    }
    
    template<class T>
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
        for (auto&& size_func : size_functions)
        {
            sum += size_func(*this);
        }
        // gotta be careful about this overflowing
        return sum;
    }
    
    ~heterogeneous_container()
    {
        clear();
    }   
    
    template<class T>
    void visit(T&& visitor)
    {
        visit_impl(visitor, typename std::decay_t<T>::types{});
    }
    
    private:
    template<class T>
    static std::unordered_map<const heterogeneous_container*, std::vector<T>> items;
    
    template<class T, class U>
    using visit_function = decltype(std::declval<T>().operator()(std::declval<U&>()));

    template<class T, class U>
    static constexpr bool has_visit_v = std::experimental::is_detected<visit_function, T, U>::value;
      
    template<class T, template<class...> class TLIST, class... TYPES>
    void visit_impl(T&& visitor, TLIST<TYPES...>)
    {
        (..., visit_impl_help<std::decay_t<T>, TYPES>(visitor));
    }
    
    template<class T, class U>
    void visit_impl_help(T& visitor)
    {
        static_assert(has_visit_v<T, U>, "Visitors must provide a visit function accepting a reference for each type");
        for (auto&& element : items<U>[this])
        {
            visitor(element);
        }
    }
    
    std::vector<std::function<void(heterogeneous_container&)>> clear_functions;
    std::vector<std::function<void(const heterogeneous_container&, heterogeneous_container&)>> copy_functions;
    std::vector<std::function<size_t(const heterogeneous_container&)>> size_functions;
};

template<class T>
std::unordered_map<const heterogeneous_container*, std::vector<T>> heterogeneous_container::items;
}

struct print_visitor : andyg::visitor_base<int, double, char, std::string>
{
    template<class T>
    void operator()(T& _in)
    {
        std::cout << _in << " ";
    }
};
struct my_visitor : andyg::visitor_base<int, double>
{
    template<class T>
    void operator()(T& _in) 
    {
        _in +=_in;
    }
};

int main()
{
    auto print_container = [](andyg::heterogeneous_container& _in){_in.visit(print_visitor{}); std::cout << std::endl;};
    andyg::heterogeneous_container c;
    c.push_back('a');
    c.push_back(1);
    c.push_back(2.0);
    c.push_back(3);
    c.push_back(std::string{"foo"});
    std::cout << "c: ";
    print_container(c);
    andyg::heterogeneous_container c2 = c;
    std::cout << "c2: ";
    print_container(c2);
    c.clear();
    std::cout << "c after clearing c: ";
    c.visit(print_visitor{});
    std::cout << std::endl;
    std::cout << "c2 after clearing c: ";
    print_container(c2);
    c = c2;
    std::cout << "c after assignment to c2: ";
    print_container(c);
    my_visitor v;
    
    std::cout << "Visiting c (should double ints and doubles)\n";
    c.visit(v);
    std::cout << "c: ";
    print_container(c);
    
    struct string_visitor : andyg::visitor_base<std::string>
    {
        void operator()(std::string& _s) 
        {
            // append bar to all strings
            _s += "bar";
        }
    };
    std::cout << "Visiting c again (should append \"bar\" to all strings)\n";
    c.visit(string_visitor{});
    std::cout << "c: ";
    print_container(c);
    std::cout << "Size of c: " << c.size() << std::endl;
    std::cout << "Number of integers in c: " << c.number_of<int>() << std::endl;
}