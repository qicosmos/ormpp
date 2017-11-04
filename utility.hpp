//
// Created by qiyu on 10/29/17.
//
#ifndef ORM_UTILITY_HPP
#define ORM_UTILITY_HPP
#include "reflection.hpp"
#include "entity.hpp"
#include "type_mapping.hpp"

namespace ormpp{
    template<typename List>
    struct result_size;

    template<template<class...> class List, class... T>
    struct result_size<List<T...>> {
        constexpr static size_t value = (iguana::get_value<T>() + ...);
    };

    template<typename... Args>
    inline void append(std::string& sql, Args&&... args) {
        ((sql+=std::forward<Args>(args), sql+=" "),...);
    }

    template<typename... Args>
    auto sort_tuple(const std::tuple<Args...>& tp){
        if constexpr(sizeof...(Args)==2){
            auto [a, b] = tp;
            if constexpr(!std::is_same_v<decltype(a), ormpp_key>&&!std::is_same_v<decltype(a), ormpp_auto_increment_key>)
            return std::make_tuple(b, a);
            else
            return tp;
        }else{
            return tp;
        }
    }

    enum class DBType{
        mysql,
        sqlite,
        postgresql
    };

    template <typename T>
    inline constexpr auto get_type_names(DBType type){
        constexpr auto SIZE = iguana::get_value<T>();
        std::array<std::string_view, SIZE> arr = {};
        iguana::for_each(T{}, [&](auto& item, auto i){
            constexpr auto Idx = decltype(i)::value;
            using U = std::remove_reference_t<decltype(iguana::get<Idx>(std::declval<T>()))>;
            std::string_view s;
            switch (type){
                case DBType::mysql : s = ormpp_mysql::type_to_name(identity<U>{});
                    break;
                case DBType::sqlite : s = ormpp_sqlite::type_to_name(identity<U>{});
                    break;
                case DBType::postgresql : s = ormpp_postgresql::type_to_name(identity<U>{});
                    break;
            }

            arr[Idx] = s;
        });

        return arr;
    }

    template<typename T, typename U, typename V, size_t Idx, typename W>
    inline void build_condition_by_key(std::string& result, const V& val, W key){
//        if(key==iguana::get_name<T, Idx>().data()){
//            if constexpr(std::is_arithmetic_v<U>){
//                append(result, iguana::get_name<T,Idx>().data(), "=", std::to_string(val));
//            }else if constexpr(std::is_same_v<std::string, U>){
//                append(result, iguana::get_name<T,Idx>().data(), "=", val);
//            }
//        }
    }

    template <typename... Args, typename Func, std::size_t... Idx>
    inline void for_each0(const std::tuple<Args...>& t, Func&& f, std::index_sequence<Idx...>) {
        (f(std::get<Idx>(t)), ...);
    }

    template<typename T>
    inline auto generate_insert_sql(bool replace){
        std::string sql = replace?"replace into ":"insert into ";
        constexpr auto SIZE = iguana::get_value<T>();
        constexpr auto name = iguana::get_name<T>();
        append(sql, name.data(), " values(");
        for (auto i = 0; i < SIZE; ++i) {
            sql+="?";
            if(i<SIZE-1)
                sql+=", ";
            else
                sql+=");";
        }

        return sql;
    }

//    template <typename T>
    bool is_empty(const std::string& t){
        return t.empty();
    }

    template<typename T, typename... Args>
    inline auto generate_delete_sql(Args&&... where_conditon){
        std::string sql = "delete from ";
        constexpr auto SIZE = iguana::get_value<T>();
        constexpr auto name = iguana::get_name<T>();
        append(sql, name.data());
        if constexpr (sizeof...(Args)>0)
        if(!is_empty(std::forward<Args>(where_conditon)...))
            append(sql, " where ", std::forward<Args>(where_conditon)...);

        return sql;
    }

    template<typename T>
    inline bool has_key(const std::string& s){
        auto arr = iguana::get_array<T>();
        for (int i = 0; i < arr.size(); ++i) {
            if(s.find(arr[i].data())!=std::string::npos)
                return true;
        }

        return false;
    }

    template<typename T, typename... Args>
    inline auto generate_query_sql(Args&&... args){
        static_assert(sizeof...(Args)==0||sizeof...(Args)>0);
        std::string sql = "select * from ";
        constexpr auto SIZE = iguana::get_value<T>();
        constexpr auto name = iguana::get_name<T>();
        append(sql, name.data());

        if constexpr (sizeof...(Args)>0){
            int i = 0;
            for_each0(std::tuple(std::forward<Args>(args)...), [&i, &sql](const auto& item){
                if(i==0&&has_key<T>(item))
                    append(sql, " where ", item);
                else
                    append(sql, " ", item);

                i++;
            }, std::make_index_sequence<sizeof...(Args)>{});
        }

        return sql;
    }

    template<typename T>
    inline constexpr auto to_str(T&& t){
        if constexpr(std::is_arithmetic_v<std::decay_t<T>>){
            return std::to_string(std::forward<T>(t));
        } else{
            return std::string("'")+t+ std::string("'");
        }
    }

    inline void get_str(std::string& sql, const std::string& s){
        auto pos = sql.find_first_of('?');
        sql.replace(pos, 1, " ");
        sql.insert(pos, s);
    }

    template<typename... Args>
    inline std::string get_sql(const std::string& o, Args&&... args){
        auto sql = o;
        std::string s = "";
        (get_str(sql, to_str(args)), ...);

        return sql;
    }
}

#endif //ORM_UTILITY_HPP
