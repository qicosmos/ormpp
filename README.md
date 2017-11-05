# ormpp--一个很酷的Modern C++ ORM库 #

[ormpp](https://github.com/qicosmos/ormpp)是modern c++(c++11/14/17)开发的ORM库，为数据库操作提供了统一、灵活和易用的接口，目前支持了三种数据库：mysql, postgresql和sqlite。ormpp主要有以下几个特点：

1. header only
1. cross platform
1. unified interface
1. easy to use
1. easy to change database

你很容易就可以实现数据库的各种操作了，大部情况下甚至都不需要写sql语句了。ormpp是基于编译期反射的，会帮你实现自动化的实体映射，你再也不用写对象到数据表相互赋值的繁琐易出错的代码了，更酷的是你可以很方便地切换数据库，如果需要从mysql切换到postgresql或sqlite只用修改一行代码就可以实现切换。

编译需要支持c++17的编译器，gcc7.2， clang4.0，vs2017 upate5+.

让我们来看看如何使用ormpp吧:

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
		for(auot& person : result){
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
	
如何你想把数据库换成postgresql或sqlite，你仅仅需要将mysql类型换成postgresql或sqlite, 其他代码不需要做任何修改，非常简单。

ormpp让数据库编程变得简单，enjoin it!
