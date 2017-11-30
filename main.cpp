#include <iostream>
#include "mysql.hpp"
#include "sqlite.hpp"
#include "postgresql.hpp"
#include "dbng.hpp"

#define TEST_MAIN
#include "unit_test.hpp"

using namespace ormpp;
const char* ip = "127.0.0.1"; //your database ip

TEST_CASE(orm_connect){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    TEST_REQUIRE(mysql.disconnect());
    TEST_REQUIRE(postgres.disconnect());
    TEST_REQUIRE(sqlite.disconnect());

    int timeout = 5;
    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb", timeout));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb", timeout));
}

TEST_CASE(orm_create_table){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"id"};
    ormpp_not_null not_null{{"id", "age"}};
    ormpp_auto_increment_key auto_key{"id"};

    TEST_REQUIRE(mysql.create_datatable<person>());
    TEST_REQUIRE(postgres.create_datatable<person>());
    TEST_REQUIRE(sqlite.create_datatable<person>());

    TEST_REQUIRE(mysql.create_datatable<person>(key));
    TEST_REQUIRE(postgres.create_datatable<person>(key));
    TEST_REQUIRE(sqlite.create_datatable<person>(key));

    TEST_REQUIRE(mysql.create_datatable<person>(not_null));
    TEST_REQUIRE(postgres.create_datatable<person>(not_null));
    TEST_REQUIRE(sqlite.create_datatable<person>(not_null));

    TEST_REQUIRE(mysql.create_datatable<person>(key, not_null));
    TEST_REQUIRE(postgres.create_datatable<person>(key, not_null));
    TEST_REQUIRE(sqlite.create_datatable<person>(key, not_null));

    TEST_REQUIRE(mysql.create_datatable<person>(not_null, key));
    TEST_REQUIRE(postgres.create_datatable<person>(not_null, key));
    TEST_REQUIRE(sqlite.create_datatable<person>(not_null, key));

    TEST_REQUIRE(mysql.create_datatable<person>(auto_key));
    TEST_REQUIRE(postgres.create_datatable<person>(auto_key));
    TEST_REQUIRE(sqlite.create_datatable<person>(auto_key));

    TEST_REQUIRE(mysql.create_datatable<person>(auto_key, not_null));
    TEST_REQUIRE(postgres.create_datatable<person>(auto_key, not_null));
    TEST_REQUIRE(sqlite.create_datatable<person>(auto_key, not_null));

    TEST_REQUIRE(mysql.create_datatable<person>(not_null, auto_key));
    TEST_REQUIRE(postgres.create_datatable<person>(not_null, auto_key));
    TEST_REQUIRE(sqlite.create_datatable<person>(not_null, auto_key));
}

TEST_CASE(orm_insert_query){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"code"};
    ormpp_not_null not_null{{"code", "age"}};
    ormpp_auto_increment_key auto_key{"code"};

    student s = {1, "tom", 0, 19, 1.5, "room2"};
    student s1 = {1, "jack", 1, 20, 2.5, "room3"};
    student s2 = {1, "mke", 2, 21, 3.5, "room4"};
    std::vector<student> v{s1, s2};

    //auto key
    {
        TEST_REQUIRE(mysql.create_datatable<student>(auto_key, not_null));
        TEST_REQUIRE(postgres.create_datatable<student>(auto_key, not_null));
        TEST_REQUIRE(sqlite.create_datatable<student>(auto_key));

        TEST_CHECK(mysql.insert(s)==1);
        auto result1 = mysql.query<student>();
        TEST_CHECK(result1.size()==1);
        TEST_CHECK(mysql.insert(s)==1);

        TEST_CHECK(postgres.insert(s)==1);
        auto result2 = postgres.query<student>();
        TEST_CHECK(result2.size()==1);

        TEST_CHECK(sqlite.insert(s)==1);
        auto result3 = sqlite.query<student>();
        TEST_CHECK(result3.size()==1);

        TEST_CHECK(mysql.insert(v)==2);
        auto result4 = mysql.query<student>();
        TEST_CHECK(result4.size()==4);

        TEST_CHECK(postgres.insert(v)==2);
        auto result5 = mysql.query<student>();
        TEST_CHECK(result5.size()==4);

        TEST_CHECK(sqlite.insert(v)==2);
        auto result6 = mysql.query<student>();
        TEST_CHECK(result6.size()==4);
    }

    //key
    {
        TEST_REQUIRE(mysql.create_datatable<student>(key, not_null));
        TEST_REQUIRE(postgres.create_datatable<student>(key, not_null));
        TEST_REQUIRE(sqlite.create_datatable<student>(key));

        v[0].code = 1;
        v[1].code = 2;
        TEST_CHECK(mysql.insert(s)==1);
        auto result1 = mysql.query<student>();
        TEST_CHECK(result1.size()==1);
        TEST_CHECK(mysql.insert(s)<0);

        TEST_CHECK(postgres.insert(s)==1);
        auto result2 = postgres.query<student>();
        TEST_CHECK(result2.size()==1);

        TEST_CHECK(sqlite.insert(s)==1);
        auto result3 = sqlite.query<student>();
        TEST_CHECK(result3.size()==1);

        TEST_CHECK(mysql.delete_records<student>());
        TEST_CHECK(mysql.insert(v)==2);
        auto result4 = mysql.query<student>();
        TEST_CHECK(result4.size()==2);

        TEST_CHECK(postgres.delete_records<student>());
        TEST_CHECK(postgres.insert(v)==2);
        auto result5 = mysql.query<student>();
        TEST_CHECK(result5.size()==2);

        TEST_CHECK(sqlite.delete_records<student>());
        TEST_CHECK(sqlite.insert(v)==2);
        auto result6 = mysql.query<student>();
        TEST_CHECK(result6.size()==2);
    }

}

TEST_CASE(orm_update){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"code"};
    ormpp_not_null not_null{{"code", "age"}};
    ormpp_auto_increment_key auto_key{"code"};

    student s = {1, "tom", 0, 19, 1.5, "room2"};
    student s1 = {2, "jack", 1, 20, 2.5, "room3"};
    student s2 = {3, "mke", 2, 21, 3.5, "room4"};
    std::vector<student> v{s, s1, s2};

    TEST_REQUIRE(mysql.create_datatable<student>(key, not_null));
    TEST_REQUIRE(postgres.create_datatable<student>(key, not_null));
    TEST_REQUIRE(sqlite.create_datatable<student>(key));

    TEST_CHECK(mysql.insert(v)==3);
    TEST_CHECK(postgres.insert(v)==3);
    TEST_CHECK(sqlite.insert(v)==3);

    v[0].name = "test1";
    v[1].name = "test2";

    TEST_CHECK(mysql.update(v[0])==1);
    auto result = mysql.query<student>();
    TEST_CHECK(mysql.update(v[1])==1);
    auto result1 = mysql.query<student>();

    TEST_CHECK(postgres.update(v[0])==1);
    auto result2 = postgres.query<student>();
    TEST_CHECK(postgres.update(v[1])==1);
    auto result3 = postgres.query<student>();

    TEST_CHECK(sqlite.update(v[0])==1);
    auto result4 = sqlite.query<student>();
    TEST_CHECK(sqlite.update(v[1])==1);
    auto result5 = sqlite.query<student>();
}

TEST_CASE(orm_multi_update){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"code"};
    ormpp_not_null not_null{{"code", "age"}};
    ormpp_auto_increment_key auto_key{"code"};

    student s = {1, "tom", 0, 19, 1.5, "room2"};
    student s1 = {2, "jack", 1, 20, 2.5, "room3"};
    student s2 = {3, "mike", 2, 21, 3.5, "room4"};
    std::vector<student> v{s, s1, s2};

    TEST_REQUIRE(mysql.create_datatable<student>(key, not_null));
    TEST_REQUIRE(postgres.create_datatable<student>(key, not_null));
    TEST_REQUIRE(sqlite.create_datatable<student>(key));

    TEST_CHECK(mysql.insert(v)==3);
    TEST_CHECK(postgres.insert(v)==3);
    TEST_CHECK(sqlite.insert(v)==3);

    v[0].name = "test1";
    v[1].name = "test2";
    v[2].name = "test3";

    TEST_CHECK(mysql.update(v)==3);
    auto result = mysql.query<student>();
    TEST_CHECK(result.size()==3);

    TEST_CHECK(postgres.update(v)==3);
    auto result2 = postgres.query<student>();
    TEST_CHECK(result2.size()==3);

    TEST_CHECK(sqlite.update(v)==3);
    auto result4 = sqlite.query<student>();
    TEST_CHECK(result4.size()==3);
}

TEST_CASE(orm_delete){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"code"};
    ormpp_not_null not_null{{"code", "age"}};
    ormpp_auto_increment_key auto_key{"code"};

    student s = {1, "tom", 0, 19, 1.5, "room2"};
    student s1 = {2, "jack", 1, 20, 2.5, "room3"};
    student s2 = {3, "mike", 2, 21, 3.5, "room4"};
    std::vector<student> v{s, s1, s2};

    TEST_REQUIRE(mysql.create_datatable<student>(key, not_null));
    TEST_REQUIRE(postgres.create_datatable<student>(key, not_null));
    TEST_REQUIRE(sqlite.create_datatable<student>(key));

    TEST_CHECK(mysql.insert(v)==3);
    TEST_CHECK(postgres.insert(v)==3);
    TEST_CHECK(sqlite.insert(v)==3);

    TEST_REQUIRE(mysql.delete_records<student>("code=1"));
    TEST_CHECK(mysql.query<student>().size()==2);
    TEST_REQUIRE(mysql.delete_records<student>(""));
    auto result = mysql.query<student>();
    TEST_CHECK(result.size()==0);

    TEST_REQUIRE(postgres.delete_records<student>("code=1"));
    TEST_CHECK(postgres.query<student>().size()==2);
    TEST_REQUIRE(postgres.delete_records<student>(""));
    auto result1 = postgres.query<student>();
    TEST_CHECK(result1.size()==0);

    TEST_REQUIRE(sqlite.delete_records<student>("code=1"));
    TEST_CHECK(sqlite.query<student>().size()==2);
    TEST_REQUIRE(sqlite.delete_records<student>(""));
    auto result2 = sqlite.query<student>();
    TEST_CHECK(result2.size()==0);
}

TEST_CASE(orm_query){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"id"};

    simple s1 = {1, 2.5, 3};
    simple s2 = {2, 3.5, 4};
    simple s3 = {3, 4.5, 5};
    std::vector<simple> v{s1, s2, s3};
    TEST_REQUIRE(mysql.create_datatable<simple>(key));
    TEST_REQUIRE(postgres.create_datatable<simple>(key));
    TEST_REQUIRE(sqlite.create_datatable<simple>(key));

    TEST_CHECK(mysql.insert(v)==3);
    TEST_CHECK(postgres.insert(v)==3);
    TEST_CHECK(sqlite.insert(v)==3);

    auto result = mysql.query<simple>();
    TEST_CHECK(result.size()==3);

    auto result1 = postgres.query<simple>();
    TEST_CHECK(result1.size()==3);

    auto result2 = sqlite.query<simple>();
    TEST_CHECK(result2.size()==3);

    auto result3 = mysql.query<simple>("id=1");
    TEST_CHECK(result3.size()==1);

    auto result4 = postgres.query<simple>("id=2");
    TEST_CHECK(result4.size()==1);

    auto result5 = sqlite.query<simple>("id=3");
    TEST_CHECK(result5.size()==1);
}

TEST_CASE(orm_query_some){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"code"};
    ormpp_not_null not_null{{"code", "age"}};
    ormpp_auto_increment_key auto_key{"code"};

    student s = {1, "tom", 0, 19, 1.5, "room2"};
    student s1 = {2, "jack", 1, 20, 2.5, "room3"};
    student s2 = {3, "mike", 2, 21, 3.5, "room4"};
    std::vector<student> v{s, s1, s2};

    TEST_REQUIRE(mysql.create_datatable<student>(key, not_null));
    TEST_REQUIRE(postgres.create_datatable<student>(key, not_null));
    TEST_REQUIRE(sqlite.create_datatable<student>(key));

    TEST_CHECK(mysql.insert(v)==3);
    TEST_CHECK(postgres.insert(v)==3);
    TEST_CHECK(sqlite.insert(v)==3);

    auto result = mysql.query<std::tuple<int, std::string, double>>("select code, name, dm from student");
    TEST_CHECK(result.size()==3);

    auto result1 = postgres.query<std::tuple<int, std::string, double>>("select code, name, dm from student");
    TEST_CHECK(result1.size()==3);

    auto result2 = sqlite.query<std::tuple<int, std::string, double>>("select code, name, dm from student");
    TEST_CHECK(result2.size()==3);

    auto result3 = mysql.query<std::tuple<int>>("select count(1) from student");
    TEST_CHECK(result3.size()==1);
    TEST_CHECK(std::get<0>(result3[0])==3);

    auto result4 = mysql.query<std::tuple<int>>("select count(1) from student");
    TEST_CHECK(result4.size()==1);
    TEST_CHECK(std::get<0>(result4[0])==3);

    auto result5 = mysql.query<std::tuple<int>>("select count(1) from student");
    TEST_CHECK(result5.size()==1);
    TEST_CHECK(std::get<0>(result5[0])==3);
}

TEST_CASE(orm_query_multi_table){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"code"};
    ormpp_not_null not_null{{"code", "age"}};
    ormpp_auto_increment_key auto_key{"code"};

    student s = {1, "tom", 0, 19, 1.5, "room2"};
    student s1 = {2, "jack", 1, 20, 2.5, "room3"};
    student s2 = {3, "mike", 2, 21, 3.5, "room4"};
    std::vector<student> v{s, s1, s2};

    TEST_REQUIRE(mysql.create_datatable<student>(key, not_null));
    TEST_REQUIRE(postgres.create_datatable<student>(key, not_null));
    TEST_REQUIRE(sqlite.create_datatable<student>(key));

    TEST_CHECK(mysql.insert(v)==3);
    TEST_CHECK(postgres.insert(v)==3);
    TEST_CHECK(sqlite.insert(v)==3);

    ormpp_key key1{"id"};
    person p = {1, "test1", 2};
    person p1 = {2, "test2", 3};
    person p2 = {3, "test3", 4};
    std::vector<person> v1{p, p1, p2};
    TEST_REQUIRE(mysql.create_datatable<person>(key1, not_null));
    TEST_REQUIRE(postgres.create_datatable<person>(key1, not_null));
    TEST_REQUIRE(sqlite.create_datatable<person>(key1));

    TEST_CHECK(mysql.insert(v1)==3);
    TEST_CHECK(postgres.insert(v1)==3);
    TEST_CHECK(sqlite.insert(v1)==3);

    auto result = mysql.query<std::tuple<person, std::string, int>>("select person.*, student.name, student.age from person, student"s);
    TEST_CHECK(result.size()==9);

    auto result1 = postgres.query<std::tuple<int, std::string, double>>("select person.*, student.name, student.age from person, student"s);
    TEST_CHECK(result1.size()==9);

    auto result2 = sqlite.query<std::tuple<int, std::string, double>>("select person.*, student.name, student.age from person, student"s);
    TEST_CHECK(result2.size()==9);

    auto result3 = mysql.query<std::tuple<person, student>>("select * from person, student"s);
    TEST_CHECK(result.size()==9);

    auto result4 = postgres.query<std::tuple<person, student>>("select * from person, student"s);
    TEST_CHECK(result1.size()==9);

    auto result5 = sqlite.query<std::tuple<person, student>>("select * from person, student"s);
    TEST_CHECK(result2.size()==9);
}

TEST_CASE(orm_transaction){
    dbng<mysql> mysql;
    dbng<sqlite> sqlite;
    dbng<postgresql> postgres;

    TEST_REQUIRE(mysql.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(postgres.connect(ip, "root", "12345", "testdb"));
    TEST_REQUIRE(sqlite.connect("test.db"));

    ormpp_key key{"code"};
    ormpp_not_null not_null{{"code", "age"}};
    ormpp_auto_increment_key auto_key{"code"};

    student s = {1, "tom", 0, 19, 1.5, "room2"};
    student s1 = {2, "jack", 1, 20, 2.5, "room3"};
    student s2 = {3, "mike", 2, 21, 3.5, "room4"};
    std::vector<student> v{s, s1, s2};

    TEST_REQUIRE(mysql.create_datatable<student>(key, not_null));
    TEST_REQUIRE(postgres.create_datatable<student>(key, not_null));
    TEST_REQUIRE(sqlite.create_datatable<student>(key));

    TEST_REQUIRE(mysql.begin());
    for (int i = 0; i < 10; ++i) {
        student s = {i, "tom", 0, 19, 1.5, "room2"};
        if(!mysql.insert(s)){
            mysql.rollback();
            return;
        }
    }
    TEST_REQUIRE(mysql.commit());
    auto result = mysql.query<student>();
    TEST_CHECK(result.size()==10);

    TEST_REQUIRE(postgres.begin());
    for (int i = 0; i < 10; ++i) {
        student s = {i, "tom", 0, 19, 1.5, "room2"};
        if(!postgres.insert(s)){
            postgres.rollback();
            return;
        }
    }
    TEST_REQUIRE(postgres.commit());
    auto result1 = postgres.query<student>();
    TEST_CHECK(result1.size()==10);

    TEST_REQUIRE(sqlite.begin());
    for (int i = 0; i < 10; ++i) {
        student s = {i, "tom", 0, 19, 1.5, "room2"};
        if(!sqlite.insert(s)){
            sqlite.rollback();
            return;
        }
    }
    TEST_REQUIRE(sqlite.commit());
    auto result2 = sqlite.query<student>();
    TEST_CHECK(result2.size()==10);
}

struct log{
    template<typename... Args>
    bool before(Args... args){
        std::cout<<"log before"<<std::endl;
        return true;
    }

    template<typename T, typename... Args>
    bool after(T t, Args... args){
        std::cout<<"log after"<<std::endl;
        return true;
    }
};

struct validate{
    template<typename... Args>
    bool before(Args... args){
        std::cout<<"validate before"<<std::endl;
        return true;
    }

    template<typename T, typename... Args>
    bool after(T t, Args... args){
        std::cout<<"validate after"<<std::endl;
        return true;
    }
};

TEST_CASE(orm_aop){
    dbng<mysql> mysql;
    auto r = mysql.warper_connect<log, validate>("127.0.0.1", "root", "12345", "testdb");
    TEST_REQUIRE(r);

    r = mysql.warper_execute("drop table if exists person");
    TEST_REQUIRE(r);

    r = mysql.warper_execute<log>("drop table if exists person");
    TEST_REQUIRE(r);

    r = mysql.warper_execute<validate>("drop table if exists person");
    TEST_REQUIRE(r);

    r = mysql.warper_execute<validate, log>("drop table if exists person");
    TEST_REQUIRE(r);
}