#include "dbng.hpp"
#include "mysql.hpp"
using namespace ormpp;
struct type_tb
{
	int id;
	std::string content;
	DateTime time;
	Integer number;
	TinyInt tiny;
	SmallInt smit;
	BigInt big;
	Float ff;
	Double dd;
	SQLDate date;
	SQLTime ttime;
};
REFLECTION(type_tb, id, content, time, number, tiny, smit, big, ff,dd, date, ttime)
int main()
{
	dbng<mysql> connect;
	connect.connect("127.0.0.1", "root", "root", "mysql_test");
	//type_tb info;
	//info.id = 0;
	//info.content = "abc";
	////info.number = 10;
	//info.tiny = 10;
	//info.smit = 32767;
	//info.ff = 20.123;
	//info.dd = 1024.123456789;
	//info.date = std::time(nullptr);
	//info.ttime = std::time(nullptr);
	////info.big = 0;
	////info.time.format_from_timestamp(std::time(nullptr));
	//connect.insert(info);
	auto vec = connect.query<type_tb>("select * from type_tb where id=44");
	//std::cout << vec.size() << std::endl;
	auto info = vec[0];
	std::cout << info.number.get_value() << std::endl;
	std::cout << info.time.get_value() << std::endl;
	std::cin.get();
	return 0;
}