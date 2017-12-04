# A Cool Modern C ++ ORM Library - ormpp

## Dictionary

* [motiviations](#motiviations)
* [features](#features)
* [example](#example)
* [compile](#compile)
* [interface](#interface)
* [roadmap](#roadmap)
* [contact](#contact)

## motiviations

The most important thing of ormpp is to simplify database programming in C++. ormpp provides easy to use unified interface, supports multiple databases, and reduces the learning cost of using the database.

## features

ormpp is an ORM library developed by modern C++(C++ 11/14/17) and currently supports three databases: mysql, postgresql and sqlite. Ormpp has the following features:

1.	header only
2.	cross platform
3.	unified interface
4.	easy to use
5.	easy to change database

You can operate a database very easily with ormpp, and in most cases you don’t even need to write any sql statement. 

Because ormpp is based on compile-time reflection, so it can do entity relation mapping automatically, you no longer have to write detailed, error-prone code, which is used to assign the object to the datatable or the datatable to the object.

Even cooler is that you can easily change the database, if you want to change mysql to postgresql or sqlite , you just need to modify the database type without modifying any other code.

## example

This example demonstrates how to use ormpp to add, delete, update datatable and do transaction with ormpp without write any sql statement.


	#include "dbng.hpp"
	using namespace ormpp;
	
	struct person
	{
		int id;
		std::string name;
		int age;
	};
	REFLECTION(person, id, name, age)
	
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
	        person s = {i, "tom" 19};
	            if(!mysql.insert(s)){
	                mysql.rollback();
	                return -1;
	            }
		}
		mysql.commit();
	}

## compile

### Compiler support

Require compiler supporting C++ 17. gcc7.2, clang 4.0, vs2017 update5+

### Database installation

Because ormpp supports mysql, postgresql and sqlite, so you need to install mysql, postgresql, libpq. After installation, configure the directory and the library path in CMakeLists.txt.

### Third part library

Depend on iguana, git clone https://github.com/qicosmos/iguana.git in ormpp directory.

After the above three steps you can compile ormpp directly.


## interface

ormpp hides the difference of the database operation interface and provides unified and easy to use interface. ormpp provides some database operation interface, such as connecting/disconnecting a database, creating a data table, inserting data, updating data, deleting data, querying data and transaction related interface.

### interface overview

	//connection
	template <typename... Args>
	bool connect(Args&&... args);
	
	//disconnection 
	bool disconnect();
	
	//create a table
	template<typename T, typename... Args>
	bool create_datatable(Args&&... args);
	
	//insert single record
	template<typename T, typename... Args>
	int insert(const T& t, Args&&... args);
	
	//insert multiple records
	template<typename T, typename... Args>
	int insert(const std::vector<T>& t, Args&&... args);
	
	//update single record
	template<typename T, typename... Args>
	int update(const T& t, Args&&... args);
	
	//update multiple records
	template<typename T, typename... Args>
	int update(const std::vector<T>& t, Args&&... args);
	
	//delete the record
	template<typename T, typename... Args>
	bool delete_records(Args&&... where_conditon);
	
	//query，including single-table query and multi-table query
	template<typename T, typename... Args>
	auto query(Args&&... args);
	
	//execute the native sql statements
	int execute(const std::string& sql);
	
	//begin a transaction
	bool begin();
	
	//begin a transaction
	bool commit();
	
	//rollback a transaction
	bool rollback();

### concrete interface introduction

Define the business entity (corresponding to the data table) in entity.hpp, and then define the database object.

	#include "dbng.hpp"
	using namespace ormpp;
	
	struct person
	{
		int id;
		std::string name;
		int age;
	};
	REFLECTION(person, id, name, age)

	int main(){

		dbng<mysql> mysql;
	    dbng<sqlite> sqlite;
	    dbng<postgresql> postgres;
		//......
	}

1. connect a database

	template <typename... Args>
	bool connect(Args&&... args);

connect exmple:

	mysql.connect("127.0.0.1", "root", "12345", "testdb")

	postgres.connect("127.0.0.1", "root", "12345", "testdb")

	sqlite.connect("127.0.0.1", "root", "12345", "testdb")

return value：bool，success return true, failure return false.

2. disconnect a database
	
	bool disconnect();

disconnect exmple:

	mysql.disconnect();

	postgres.disconnect();

	sqlite.disconnect();

note：don't need call disconnect explicitly，because the interface will be called automatically when the database object destruct.

return value：bool，success return true, failure return false.

3.create a datatable

	template<typename T, typename... Args>
	bool create_datatable(Args&&... args);

create_datatable example:

	//create a datatable without a primary key
	mysql.create_datatable<student>();
	
	postgres.create_datatable<student>();
	
	sqlite.create_datatable<student>();

	//create a datatable with a primary key and not null attribute
	ormpp_key key1{"id"};
	ormpp_not_null not_null{{"id", "age"}};

    person p = {1, "test1", 2};
    person p1 = {2, "test2", 3};
    person p2 = {3, "test3", 4};

    mysql.create_datatable<person>(key1, not_null);
    postgres.create_datatable<person>(key1, not_null);
    sqlite.create_datatable<person>(key1);

note：now just support key and not null attributes，multiple primary keys will be supported in next version。

return value：bool，success return true, failure return false.

4.insert single record

	template<typename T, typename... Args>
	int insert(const T& t, Args&&... args);

insert example:

	person p = {1, "test1", 2};
	TEST_CHECK(mysql.insert(p)==1);
    TEST_CHECK(postgres.insert(p)==1);
    TEST_CHECK(sqlite.insert(p)==1);

return value：int，success return 1，failure return INT_MIN.

5.insert multiple records

	template<typename T, typename... Args>
	int insert(const std::vector<T>& t, Args&&... args);

multiple insert example:

	person p = {1, "test1", 2};
    person p1 = {2, "test2", 3};
    person p2 = {3, "test3", 4};
    std::vector<person> v1{p, p1, p2};

    TEST_CHECK(mysql.insert(v1)==3);
    TEST_CHECK(postgres.insert(v1)==3);
    TEST_CHECK(sqlite.insert(v1)==3);

return value：int，success return N，failure return INT_MIN.

6. update single record


	template<typename T, typename... Args>
	int update(const T& t, Args&&... args);

update example:

	person p = {1, "test1", 2};
	TEST_CHECK(mysql.update(p)==1);
    TEST_CHECK(postgres.update(p)==1);
    TEST_CHECK(sqlite.update(p)==1);

note：update by the primary key when the primary key exists，otherwise you need give a key name，for example
	
	TEST_CHECK(mysql.update(p, "age")==1);
    TEST_CHECK(postgres.update(p, "age")==1);
    TEST_CHECK(sqlite.update(p, "age")==1);

return value：int，success return 1，failure return INT_MIN.

5.update multiple records

	template<typename T, typename... Args>
	int update(const std::vector<T>& t, Args&&... args);

multiple insert example:

	person p = {1, "test1", 2};
    person p1 = {2, "test2", 3};
    person p2 = {3, "test3", 4};
    std::vector<person> v1{p, p1, p2};

    TEST_CHECK(mysql.insert(v1)==3);
    TEST_CHECK(postgres.insert(v1)==3);
    TEST_CHECK(sqlite.insert(v1)==3);

note：update by the primary key when the primary key exists，otherwise you need give a key name，as above.

return value：int，success return N，failure return INT_MIN.

6. delete the record

	template<typename T, typename... Args>
	bool delete_records(Args&&... where_conditon);

delete_records example:

	//delete all
	TEST_REQUIRE(mysql.delete_records<person>());
	TEST_REQUIRE(postgres.delete_records<person>());
	TEST_REQUIRE(sqlite.delete_records<person>());

	//delete according some conditions
	TEST_REQUIRE(mysql.delete_records<person>("id=1"));
	TEST_REQUIRE(postgres.delete_records<person>("id=1"));
	TEST_REQUIRE(sqlite.delete_records<person>("id=1"));

return value：bool，success return true, failure return false.

7.query a table

	template<typename T, typename... Args>
	auto query(Args&&... args);

	//if T is a reflection object return vector<T>
	template<typename T, typename... Args>
	std::vector<T> query(Args&&... args);

single table query example:

    auto result = mysql.query<person>();
    TEST_CHECK(result.size()==3);

    auto result1 = postgres.query<person>();
    TEST_CHECK(result1.size()==3);

    auto result2 = sqlite.query<person>();
    TEST_CHECK(result2.size()==3);

	//query according some conditions
    auto result3 = mysql.query<person>("id=1");
    TEST_CHECK(result3.size()==1);

    auto result4 = postgres.query<person>("id=2");
    TEST_CHECK(result4.size()==1);

    auto result5 = sqlite.query<person>("id=3");

return value：std::vector<T>，success return not empty vector，failure return empty vector.

8.multi-query or the specific field query

	template<typename T, typename... Args>
	auto query(Args&&... args);

	//if T is a tuple return vector<tuple<T>>
	template<typename T, typename... Args>
	std::vector<std::tuple<T>> query(Args&&... args);

multiple or some fields query example:

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

return value：std::vector<std::tuple<T>>，success return not empty vector，failure return empty vector.

9.execute the native sql statements

	int execute(const std::string& sql);

execute example:

	r = mysql.execute("drop table if exists person");
    TEST_REQUIRE(r);

    r = postgres("drop table if exists person");
    TEST_REQUIRE(r);

    r = sqlite.execute("drop table if exists person");
    TEST_REQUIRE(r);

note：the sql statement should not contain any placeholder，and a complete sql statement.

return value：int，success return 1，failure return INT_MIN.

10.transaction interface

begin, commit, rollback a transaction

	//transaction
	mysql.begin();
	for (int i = 0; i < 10; ++i) {
        person s = {i, "tom" 19};
            if(!mysql.insert(s)){
                mysql.rollback();
                return -1;
            }
	}
	mysql.commit();
return value：bool，success return true, failure return false.

11.AOP

define aspects：

	struct log{
	    //args...  arguments of business function
	    template<typename... Args>
	    bool before(Args... args){
	        std::cout<<"log before"<<std::endl;
	        return true;
	    }
	
	    //result: return value of business function; args...  arguments of business function
	    template<typename T, typename... Args>
	    bool after(T result, Args... args){
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
	    bool after(T result, Args... args){
	        std::cout<<"validate after"<<std::endl;
	        return true;
	    }
	};

note：in an aspect，you can define the before method or after method，or both。

	//add logging and validate aspect
	dbng<mysql> mysql;
    auto r = mysql.warper_connect<log, validate>("127.0.0.1", "root", "12345", "testdb");
    TEST_REQUIRE(r);

## roadmap

1. support multiple keys.
1. add some predicates when query multiple tables, such as where, group, oder by, join, limit etc，avoid write sql statements directly.
2. add logging.
3. add a getting error messages interface.
4. support more databases.


## contact

purecpp@163.com

[http://purecpp.org/](http://purecpp.org/ "purecpp")

[https://github.com/qicosmos/ormpp](https://github.com/qicosmos/ormpp "ormpp")
