//
// Created by qiyu on 10/23/17.
//
#ifdef _MSC_VER
#include <include/mysql.h>
#else
#include <mysql/mysql.h>
#endif
#include <sqlite3.h>
#include <string>
#include <string_view>
#include "pg_types.h"
using namespace std::string_view_literals;

#ifndef EXAMPLE1_TYPE_MAPPING_HPP
#define EXAMPLE1_TYPE_MAPPING_HPP

namespace ormpp{
    template <class T>
    struct identity{};

#define REGISTER_TYPE(Type, Index)                                              \
    constexpr int type_to_id(identity<Type>) noexcept { return Index; } \
    constexpr auto id_to_type( std::integral_constant<std::size_t, Index > ) noexcept { Type res{}; return res; }

    namespace ormpp_mysql{
        REGISTER_TYPE( char    , MYSQL_TYPE_TINY     )
        REGISTER_TYPE( short   , MYSQL_TYPE_SHORT    )
        REGISTER_TYPE( int     , MYSQL_TYPE_LONG     )
        REGISTER_TYPE( float   , MYSQL_TYPE_FLOAT    )
        REGISTER_TYPE( double  , MYSQL_TYPE_DOUBLE   )
        REGISTER_TYPE( int64_t , MYSQL_TYPE_LONGLONG )

        constexpr int type_to_id(identity<std::string>) noexcept { return MYSQL_TYPE_VAR_STRING; }
        std::string id_to_type(std::integral_constant<std::size_t, MYSQL_TYPE_VAR_STRING > ) noexcept { std::string res{}; return res; }

        constexpr auto type_to_name(identity<char>) noexcept { return "TINYINT"sv; }
        constexpr auto type_to_name(identity<short>) noexcept { return "SMALLINT"sv; }
        constexpr auto type_to_name(identity<int>) noexcept { return "INTEGER"sv; }
        constexpr auto type_to_name(identity<float>) noexcept { return "FLOAT"sv; }
        constexpr auto type_to_name(identity<double>) noexcept { return "DOUBLE"sv; }
        constexpr auto type_to_name(identity<int64_t>) noexcept { return "BIGINT"sv; }
        constexpr auto type_to_name(identity<std::string>) noexcept { return "TEXT"sv; }
    }

    namespace ormpp_sqlite{
        REGISTER_TYPE( int     , SQLITE_INTEGER     )
        REGISTER_TYPE( double  , SQLITE_FLOAT   )

        constexpr int type_to_id(identity<std::string>) noexcept { return SQLITE_TEXT; }
        std::string id_to_type(std::integral_constant<std::size_t, SQLITE_TEXT > ) noexcept { std::string res{}; return res; }

        constexpr auto type_to_name(identity<char>) noexcept { return "INTEGER"sv; }
        constexpr auto type_to_name(identity<short>) noexcept { return "INTEGER"sv; }
        constexpr auto type_to_name(identity<int>) noexcept { return "INTEGER"sv; }
        constexpr auto type_to_name(identity<float>) noexcept { return "FLOAT"sv; }
        constexpr auto type_to_name(identity<double>) noexcept { return "DOUBLE"sv; }
        constexpr auto type_to_name(identity<int64_t>) noexcept { return "INTEGER"sv; }
        constexpr auto type_to_name(identity<std::string>) noexcept { return "TEXT"sv; }
    }

    namespace ormpp_postgresql{
        REGISTER_TYPE( bool    , BOOLOID     )
        REGISTER_TYPE( char    , CHAROID     )
        REGISTER_TYPE( short   , INT2OID    )
        REGISTER_TYPE( int     , INT4OID     )
        REGISTER_TYPE( float   , FLOAT4OID    )
        REGISTER_TYPE( double  , FLOAT8OID   )
        REGISTER_TYPE( int64_t , INT8OID )

        constexpr int type_to_id(identity<std::string>) noexcept { return TEXTOID; }
        std::string id_to_type(std::integral_constant<std::size_t,  TEXTOID> ) noexcept { std::string res{}; return res; }

        constexpr auto type_to_name(identity<char>) noexcept { return "char"sv; }
        constexpr auto type_to_name(identity<short>) noexcept { return "smallint"sv; }
        constexpr auto type_to_name(identity<int>) noexcept { return "integer"sv; }
        constexpr auto type_to_name(identity<float>) noexcept { return "real"sv; }
        constexpr auto type_to_name(identity<double>) noexcept { return "double precision"sv; }
        constexpr auto type_to_name(identity<int64_t>) noexcept { return "bigint"sv; }
        constexpr auto type_to_name(identity<std::string>) noexcept { return "text"sv; }
    }
}

#endif //EXAMPLE1_TYPE_MAPPING_HPP
