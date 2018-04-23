//
// Created by qiyu on 10/20/17.
//
#ifndef ORM_ENTITY_HPP
#define ORM_ENTITY_HPP
#include <set>
#include <string>

struct ormpp_not_null{
    std::set<std::string> fields;
};

struct ormpp_key{
    std::string fields;
};

struct ormpp_auto_key{
    std::string fields;
};

struct ormpp_unique {
	std::string fields;
};

#endif //ORM_ENTITY_HPP
