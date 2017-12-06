//
// Created by qiyu on 10/29/17.
//
#ifndef ORM_UTILITY_HPP
#define ORM_UTILITY_HPP
#include <iguana/reflection.hpp>
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

    template<typename  T>
    inline auto generate_auto_insert_sql(auto auto_key_map_, bool replace){
        std::string sql = replace?"replace into ":"insert into ";
        constexpr auto SIZE = iguana::get_value<T>();
        constexpr auto name = iguana::get_name<T>();
        append(sql, name.data());

        std::string fields = "(";
        std::string values = " values(";
        auto it = auto_key_map_.find(name.data());
        for (auto i = 0; i < SIZE; ++i) {
            std::string field_name = iguana::get_name<T>(i).data();
            if(it!=auto_key_map_.end()&&it->second==field_name)
                continue;

            values+="?";
            fields+=field_name;
            if(i<SIZE-1){
                fields+=", ";
                values+=", ";
            }
            else{
                fields+=")";
                values+=")";
            }
        }
        append(sql, fields, values);
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

    template<typename T>
    struct field_attribute;

    template<typename T, typename U>
    struct field_attribute<U T::*>{
        using type = T;
        using return_type = U;
    };

    template<typename U>
    constexpr std::string_view get_field_name(std::string_view full_name){
        using T = typename field_attribute<U>::type;
        return full_name.substr(iguana::get_name<T>().length()+1, full_name.length());
    }

#define FID(field) std::pair(get_field_name<decltype(&field)>(std::string_view(#field)), &field)
}

#endif //ORM_UTILITY_HPP
