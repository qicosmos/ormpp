//
// Created by qiyu on 10/20/17.
//
#ifndef ORM_ENTITY_HPP
#define ORM_ENTITY_HPP
#include <set>
#include <string>
#include "iguana/iguana/reflection.hpp"

struct ormpp_not_null{
    std::set<std::string> fields;
};

struct ormpp_key{
    std::string fields;
};

struct ormpp_auto_increment_key{
    std::string fields;
};

struct person
{
    int id;
    std::string name;
    int age;
};
REFLECTION(person, id, name, age)

struct student{
    int code;//key
    std::string name;
    char sex;
    int age;
    double dm;
    std::string classroom;
};
REFLECTION(student, code, name, sex, age, dm, classroom)

struct simple{
    int id;
    double code;
    int age;
};
REFLECTION(simple, id, code, age);

#endif //ORM_ENTITY_HPP
