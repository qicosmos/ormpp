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

struct tb_add
{
	int id;
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
//REFLECTION(tb_add, id)
REFLECTION(tb_add, id, time, number, tiny, smit, big, ff, dd, date, ttime)

int main()
{
	dbng<mysql> connect;
	connect.connect("127.0.0.1", "root", "root", "mysql_test");
	type_tb info;
	info.id = 0;
	info.content = "abc";
	//info.number = 10;
	info.tiny = 10;
	info.smit = 32767;
	info.ff = 20.123;
	info.dd = 1024.123456789;
	info.date = std::time(nullptr);
	info.ttime = std::time(nullptr);

	info.number = 1024;
	info.number.set_null();

	info.time = "2019-01-02 16:19:00";

	std::vector<type_tb> ins;
	ins.push_back(info);
	auto other = info;
	other.time = "2019-01-02 16:19:59";
	ins.push_back(other);

	connect.insert(ins);
	//connect.create_datatable<tb_add>();


	//auto vec = connect.query<type_tb>("select * from type_tb where id=44");
	//auto& info = vec[0];
	//
	//info.content = "2333333333";
	//info.time = "2019-01-02 16:19:00";
	//connect.update(info);

	//cc = '2';
	//auto vec = connect.query<std::tuple<Integer, std::string, DateTime, Integer, TinyInt, SmallInt, BigInt, Float, Double, SQLDate, SQLTime>>("select * from type_tb where id=44");
	//auto info = vec[0];
	//std::cout << std::get<0>(info) << std::endl;
	//std::cout << std::get<1>(info) << std::endl;
	//std::cout << std::get<2>(info)<< std::endl;
	//std::cout << std::get<3>(info) << std::endl;
	//std::cout << std::get<4>(info)<<std::endl;
	//std::cout << std::get<5>(info) << std::endl;
	//std::cout << std::get<6>(info) << std::endl;
	//std::cout << std::get<7>(info) << std::endl;
	//std::cout << std::get<8>(info) << std::endl;
	//std::cout << std::get<9>(info)<< std::endl;
	//std::cout << std::get<10>(info)<< std::endl;

	std::cin.get();
	return 0;
}