<p align="center">
<a href="https://github.com/qicosmos/ormpp/actions/workflows/ci-sqlite.yml">
<img alt="ci-sqlite" src="https://github.com/qicosmos/ormpp/actions/workflows/ci-sqlite.yml/badge.svg?branch=master">
</a>
<a href="https://github.com/qicosmos/ormpp/actions/workflows/ci-mysql.yml">
<img alt="ci-mysql" src="https://github.com/qicosmos/ormpp/actions/workflows/ci-mysql.yml/badge.svg?branch=master">
</a>
<a href="https://codecov.io/gh/qicosmos/ormpp">
<img alt="codecov" src="https://codecov.io/gh/qicosmos/ormpp/branch/master/graph/badge.svg">
</a>
<img alt="language" src="https://img.shields.io/github/languages/top/qicosmos/ormpp?style=flat-square">
<img alt="last commit" src="https://img.shields.io/github/last-commit/qicosmos/ormpp?style=flat-square">
</p>

<p align="center">
  <a href="https://github.com/qicosmos/ormpp/tree/master/lang/english/README.md">English</a> | <span>中文</span>
</p>

# 一个很酷的Modern C++ ORM库----ormpp

感谢Selina同学将中文wiki翻译为英文

[谁在用ormpp](https://github.com/qicosmos/ormpp/wiki), 也希望ormpp用户帮助编辑用户列表，也是为了让更多用户把ormpp用起来，也是对ormpp 最大的支持，用户列表的用户问题会优先处理。

## 目录

* [ormpp的目标](#ormpp的目标)
* [ormpp的特点](#ormpp的特点)
* [快速示例](#快速示例)
* [如何编译](#如何编译)
* [接口介绍](#接口介绍)
* [roadmap](#roadmap)
* [联系方式](#联系方式)

## ormpp的目标
ormpp最重要的目标就是让c++中的数据库编程变得简单，为用户提供统一的接口，支持多种数据库，降低用户使用数据库的难度。

## ormpp的特点
ormpp是modern c++(c++11/14/17)开发的ORM库，目前支持了三种数据库：mysql, postgresql和sqlite，ormpp主要有以下几个特点：

1. header only
1. cross platform
1. unified interface
1. easy to use
1. easy to change database

你通过ormpp可以很容易地实现数据库的各种操作了，大部情况下甚至都不需要写sql语句。ormpp是基于编译期反射的，会帮你实现自动化的实体映射，你再也不用写对象到数据表相互赋值的繁琐易出错的代码了，更酷的是你可以很方便地切换数据库，如果需要从mysql切换到postgresql或sqlite只需要修改一下数据库类型就可以了，无需修改其他代码。

## 快速示例

这个例子展示如何使用ormpp实现数据库的增删改查之类的操作，无需写sql语句。

```C++
#include "dbng.hpp"
using namespace ormpp;

struct person {
  int id;
  std::string name;
  std::optional<int> age; // 可以插入null值
};
REFLECTION(person, id, name, age)
// REFLECTION_WITH_NAME(person, "CUSTOM_TABLE_NAME", id, name, age)

int main()
{
	person p = {1, "test1", 2};
	person p1 = {2, "test2", 3};
	person p2 = {3, "test3", 4};
	std::vector<person> v{p1, p2};

	dbng<mysql> mysql;
	mysql.connect("127.0.0.1", "dbuser", "yourpwd", "testdb");
	mysql.create_datatable<person>();

	mysql.insert(p);
	mysql.insert(v);

	mysql.update(p);
	mysql.update(v);

	auto result = mysql.query<person>(); //vector<person>
	for(auto& person : result){
		std::cout<<person.id<<" "<<person.name<<" "<<person.age<<std::endl;
	}

	mysql.delete_records<person>();

	//transaction
	mysql.begin();
	for (int i = 0; i < 10; ++i) {
        person s = {i, "tom", 19};
            if(!mysql.insert(s)){
                mysql.rollback();
                return -1;
            }
	}
	mysql.commit();
}
```

## 如何编译

支持的选项如下:
	1.	ENABLE_SQLITE3
	2.	ENABLE_MYSQL
	3.	ENABLE_PG

cmake -B build -DENABLE_SQLITE3=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug

### 编译器支持

需要支持C++17的编译器, 要求的编译器版本：linux gcc7.2, clang4.0; windows >vs2017 update5

### 数据库的安装

因为ormpp支持mysql和postgresql，所以需要安装mysql，postgresql，postgresql官方提供的libpq，安装之后，在CMakeLists.txt配置目录和库路径。

## 接口介绍
ormpp屏蔽了不同数据库操作接口的差异，提供了统一简单的数据库操作接口，具体提供了数据库连接、断开连接、创建数据表、插入数据、更新数据、删除数据、查询数据和事务相关的接口。

### 接口概览

```c++
//连接数据库
template <typename... Args>
bool connect(Args&&... args);

//断开数据库连接
bool disconnect();

//创建数据表
template<typename T, typename... Args>
bool create_datatable(Args&&... args);

//插入单条数据
template<typename T, typename... Args>
int insert(const T& t, Args&&... args);

//插入多条数据
template<typename T, typename... Args>
int insert(const std::vector<T>& t, Args&&... args);

//更新单条数据
template<typename T, typename... Args>
int update(const T& t, Args&&... args);

//更新多条数据
template<typename T, typename... Args>
int update(const std::vector<T>& t, Args&&... args);

//删除数据
template<typename T, typename... Args>
bool delete_records(Args&&... where_conditon);

//查询数据，包括单表查询和多表查询
template<typename T, typename... Args>
auto query(Args&&... args);

//执行原生的sql语句
int execute(const std::string& sql);

//开始事务
bool begin();

//提交事务
bool commit();

//回滚
bool rollback();
```

### 具体的接口使用介绍
先在entity.hpp中定义业务实体（和数据库的表对应），接着定义数据库对象：

```C++
#include "dbng.hpp"
using namespace ormpp;

struct person {
  int id;
  std::string name;
  std::optional<int> age; // 插入null值
};
REFLECTION(person, id, name, age)
// REFLECTION_WITH_NAME(person, "CUSTOM_TABLE_NAME", id, name, age)

int main(){

	dbng<mysql> mysql;
  dbng<sqlite> sqlite;
  dbng<postgresql> postgres;
	//......
}
```

1. 连接数据库

	template <typename... Args>
	bool connect(Args&&... args);

connect exmple:

```C++
// mysql.connect(host, dbuser, pwd, dbname);
mysql.connect("127.0.0.1", "root", "12345", "testdb");
// mysql.connect(host, dbuser, pwd, dbname, timeout, port);
mysql.connect("127.0.0.1", "root", "12345", "testdb", 5, 3306);
  
postgres.connect("127.0.0.1", "root", "12345", "testdb");

sqlite.connect("127.0.0.1", "root", "12345", "testdb");
```

返回值：bool，成功返回true，失败返回false.

2. 断开数据库连接
	
	bool disconnect();

disconnect exmple:

```c++
mysql.disconnect();

postgres.disconnect();

sqlite.disconnect();
```

注意：用户可以不用显式调用，在数据库对象析构时会自动调用disconnect接口。

返回值：bool，成功返回true，失败返回false.

3. 创建数据表

```C++
template<typename T, typename... Args>
bool create_datatable(Args&&... args);
```

create_datatable example:

```C++
//创建不含主键的表
mysql.create_datatable<student>();

postgres.create_datatable<student>();

sqlite.create_datatable<student>();

//创建含主键和not null属性的表
ormpp_key key1{"id"};
ormpp_not_null not_null{{"id", "age"}};

person p = {1, "test1", 2};
person p1 = {2, "test2", 3};
person p2 = {3, "test3", 4};

mysql.create_datatable<person>(key1, not_null);
postgres.create_datatable<person>(key1, not_null);
sqlite.create_datatable<person>(key1);
```

注意：目前只支持了key和not null属性，并且只支持单键，还不支持组合键，将在下一个版本中支持组合键。

返回值：bool，成功返回true，失败返回false.

4. 插入单条数据

```C++
template<typename T, typename... Args>
int insert(const T& t, Args&&... args);
```

insert example:

```C++
person p = {1, "test1", 2};
TEST_CHECK(mysql.insert(p)==1);
TEST_CHECK(postgres.insert(p)==1);
TEST_CHECK(sqlite.insert(p)==1);

// age为null
person p = {1, "test1", {}};
TEST_CHECK(mysql.insert(p)==1);
TEST_CHECK(postgres.insert(p)==1);
TEST_CHECK(sqlite.insert(p)==1);
```

返回值：int，成功返回插入数据的条数1，失败返回INT_MIN.

5. 插入多条数据

```C++
template<typename T, typename... Args>
int insert(const std::vector<T>& t, Args&&... args);
```

multiple insert example:

```C++
person p = {1, "test1", 2};
person p1 = {2, "test2", 3};
person p2 = {3, "test3", 4};
std::vector<person> v1{p, p1, p2};

TEST_CHECK(mysql.insert(v1)==3);
TEST_CHECK(postgres.insert(v1)==3);
TEST_CHECK(sqlite.insert(v1)==3);
```

返回值：int，成功返回插入数据的条数N，失败返回INT_MIN.

6. 更新单条数据


```C++
template<typename T, typename... Args>
int update(const T& t, Args&&... args);
```

update example:

```C++
person p = {1, "test1", 2};
TEST_CHECK(mysql.update(p)==1);
TEST_CHECK(postgres.update(p)==1);
TEST_CHECK(sqlite.update(p)==1);
```

注意：更新会根据表的key字段去更新，如果表没有key字段的时候，需要指定一个更新依据字段名，比如
```C++
TEST_CHECK(mysql.update(p, "age")==1);
TEST_CHECK(postgres.update(p, "age")==1);
TEST_CHECK(sqlite.update(p, "age")==1);
```

返回值：int，成功返回更新数据的条数1，失败返回INT_MIN.

7. 更新多条数据

```C++
template<typename T, typename... Args>
int update(const std::vector<T>& t, Args&&... args);
```

multiple insert example:

```C++
person p = {1, "test1", 2};
person p1 = {2, "test2", 3};
person p2 = {3, "test3", 4};
std::vector<person> v1{p, p1, p2};

TEST_CHECK(mysql.update(v1)==3);
TEST_CHECK(postgres.update(v1)==3);
TEST_CHECK(sqlite.update(v1)==3);
```

注意：更新会根据表的key字段去更新，如果表没有key字段的时候，需要指定一个更新依据字段名，用法同上。

返回值：int，成功返回更新数据的条数N，失败返回INT_MIN.

8. 删除数据

template<typename T, typename... Args>
bool delete_records(Args&&... where_conditon);

delete_records example:

```C++
//删除所有数据
TEST_REQUIRE(mysql.delete_records<person>());
TEST_REQUIRE(postgres.delete_records<person>());
TEST_REQUIRE(sqlite.delete_records<person>());

//根据条件删除数据
TEST_REQUIRE(mysql.delete_records<person>("id=1"));
TEST_REQUIRE(postgres.delete_records<person>("id=1"));
TEST_REQUIRE(sqlite.delete_records<person>("id=1"));
```

返回值：bool，成功返回true，失败返回false.

9. 单表查询

```C++
template<typename T, typename... Args>
auto query(Args&&... args);

//如果T是一个反射对象则返回的是单表查询结果vector<T>
template<typename T, typename... Args>
std::vector<T> query(Args&&... args);
```

single table query example:

```C++
auto result = mysql.query<person>();
TEST_CHECK(result.size()==3);

auto result1 = postgres.query<person>();
TEST_CHECK(result1.size()==3);

auto result2 = sqlite.query<person>();
TEST_CHECK(result2.size()==3);

//可以根据条件查询
auto result3 = mysql.query<person>("id=1");
TEST_CHECK(result3.size()==1);

auto result4 = postgres.query<person>("id=2");
TEST_CHECK(result4.size()==1);

auto result5 = sqlite.query<person>("id=3");
```

返回值：std::vector<T>，成功vector不为空，失败则为空.

10. 多表或特定列查询

```C++
template<typename T, typename... Args>
auto query(Args&&... args);

//如果T是一个tuple类型则返回的是多表或特定列查询，结果vector<tuple<T>>
template<typename T, typename... Args>
std::vector<std::tuple<T>> query(Args&&... args);
```

multiple or some fields query example:

```C++
auto result = mysql.query<std::tuple<int, std::string, int>>("select code, name, dm from person");
TEST_CHECK(result.size()==3);

auto result1 = postgres.query<std::tuple<int, std::string, int>>("select code, name, dm from person");
TEST_CHECK(result1.size()==3);

auto result2 = sqlite.query<std::tuple<int, std::string, int>>("select code, name, dm from person");
TEST_CHECK(result2.size()==3);

auto result3 = mysql.query<std::tuple<int>>("select count(1) from person");
TEST_CHECK(result3.size()==1);
TEST_CHECK(std::get<0>(result3[0])==3);

auto result4 = postgres.query<std::tuple<int>>("select count(1) from person");
TEST_CHECK(result4.size()==1);
TEST_CHECK(std::get<0>(result4[0])==3);

auto result5 = sqlite.query<std::tuple<int>>("select count(1) from person");
TEST_CHECK(result5.size()==1);
TEST_CHECK(std::get<0>(result5[0])==3);
```

返回值：std::vector<std::tuple<T>>，成功vector不为空，失败则为空.

11. 执行原生sql语句

```C++
int execute(const std::string& sql);
```

execute example:

```C++
r = mysql.execute("drop table if exists person");
TEST_REQUIRE(r);

r = postgres("drop table if exists person");
TEST_REQUIRE(r);

r = sqlite.execute("drop table if exists person");
TEST_REQUIRE(r);
```

注意：execute接口支持的原生sql语句是不带占位符的，是一条完整的sql语句。

返回值：int，成功返回更新数据的条数1，失败返回INT_MIN.

12. 事务接口

开始事务，提交事务，回滚

```C++
//transaction
mysql.begin();
for (int i = 0; i < 10; ++i) {
    person s = {i, "tom", 19};
        if(!mysql.insert(s)){
            mysql.rollback();
            return -1;
        }
}
mysql.commit();
```
返回值：bool，成功返回true，失败返回false.

13. 面向切面编程AOP

定义切面：

```C++
struct log{
	//args...是业务逻辑函数的入参
    template<typename... Args>
    bool before(Args... args){
        std::cout<<"log before"<<std::endl;
        return true;
    }

	//T的类型是业务逻辑返回值，后面的参数则是业务逻辑函数的入参
    template<typename T, typename... Args>
    bool after(T t, Args... args){
        std::cout<<"log after"<<std::endl;
        return true;
    }
};

struct validate{
	//args...是业务逻辑函数的入参
    template<typename... Args>
    bool before(Args... args){
        std::cout<<"validate before"<<std::endl;
        return true;
    }

	//T的类型是业务逻辑返回值，后面的参数则是业务逻辑函数的入参
    template<typename T, typename... Args>
    bool after(T t, Args... args){
        std::cout<<"validate after"<<std::endl;
        return true;
    }
};
```

注意：切面的定义中，允许你只定义before或after，或者二者都定义。

```C++
//增加日志和校验的切面
dbng<mysql> mysql;
auto r = mysql.warper_connect<log, validate>("127.0.0.1", "root", "12345", "testdb");
TEST_REQUIRE(r);
```

## roadmap

1. 支持组合键。
1. 多表查询时增加一些诸如where, group, oder by, join, limit等常用的谓词，避免直接写sql语句。
2. 增加日志
3. 增加获取错误消息的接口
4. 支持更多的数据库
5. 增加数据库链接池


## 联系方式

purecpp@163.com

qq群: 492859173

[http://purecpp.cn/](http://purecpp.cn/ "purecpp")

[https://github.com/qicosmos/ormpp](https://github.com/qicosmos/ormpp "ormpp")
