//
// Created by qiyu on 11/3/17.
//

#ifndef ORM_DBNG_HPP
#define ORM_DBNG_HPP

#include <string>
#include <string_view>
namespace ormpp{
    template<typename DB>
    class dbng{
    public:
        //ip, user, pwd, db  the sequence must be fixed like this
        template <typename... Args>
        bool connect(Args&&... args){
            return  db_.template connect(std::forward<Args>(args)...);
        }

        template <typename... Args>
        bool disconnect(Args&&... args){
            return  db_.template disconnect(std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr auto create_datatable(Args&&... args){
            return db_.template create_datatable<T>(std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int insert(const T& t,Args&&... args){
            return db_.insert(t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int insert(const std::vector<T>& t, Args&&... args){
            return db_.insert(t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int update(const T& t, Args&&... args) {
            return db_.update(t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int update(const std::vector<T>& t, Args&&... args){
            return db_.update(t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr bool delete_records(Args&&... where_conditon){
            return db_.template delete_records<T>(std::forward<Args>(where_conditon)...);
        }

        //restriction, all the args are string, the first is the where condition, rest are append conditions
        template<typename T, typename... Args>
        constexpr auto query(Args&&... args){
            return db_.template query<T>(std::forward<Args>(args)...);
        }

        auto excecute(const std::string& sql){
            return db_.excecute(sql);
        }

        //transaction
        bool begin(){
            return db_.begin();
        }

        bool commit(){
            return db_.commit();
        }

        bool rollback(){
            return db_.rollback();
        }

    private:
#define HAS_MEMBER(member)\
template<typename T, typename... Args>\
struct has_##member\
{\
private:\
    template<typename U> static auto Check(int) -> decltype(std::declval<U>().member(std::declval<Args>()...), std::true_type()); \
	template<typename U> static std::false_type Check(...);\
public:\
	enum{value = std::is_same<decltype(Check<T>(0)), std::true_type>::value};\
};

        HAS_MEMBER(before)
        HAS_MEMBER(after)

#define WARPER(func)\
    template<typename... AP, typename... Args>\
    auto warper##_##func(Args&&... args){\
        using result_type = decltype(std::declval<decltype(this)>()->func(std::declval<Args>()...));\
        bool r = true;\
        std::tuple<AP...> tp{};\
        for_each_l(tp, [&r, &args...](auto& item){\
            if(!r)\
                return;\
            if constexpr (has_before<decltype(item)>::value)\
            r = item.before(std::forward<Args>(args)...);\
        }, std::make_index_sequence<sizeof...(AP)>{});\
        if(!r)\
            return result_type{};\
        auto lambda = [this, &args...]{ return this->func(std::forward<Args>(args)...); };\
        result_type result = std::invoke(lambda);\
        for_each_r(tp, [&r, &result, &args...](auto& item){\
            if(!r)\
                return;\
            if constexpr (has_after<decltype(item), result_type>::value)\
            r = item.after(result, std::forward<Args>(args)...);\
        }, std::make_index_sequence<sizeof...(AP)>{});\
        return result;\
    }

        template <typename... Args, typename F, std::size_t... Idx>
        constexpr void for_each_l(std::tuple<Args...>& t, F&& f, std::index_sequence<Idx...>)
        {
            (std::forward<F>(f)(std::get<Idx>(t)), ...);
        }

        template <typename... Args, typename F, std::size_t... Idx>
        constexpr void for_each_r(std::tuple<Args...>& t, F&& f, std::index_sequence<Idx...>)
        {
            constexpr auto size = sizeof...(Idx);
            (std::forward<F>(f)(std::get<size-Idx-1>(t)), ...);
        }

    public:
        WARPER(connect);
        WARPER(excecute);
    private:
        DB db_;
    };
}

#endif //ORM_DBNG_HPP
