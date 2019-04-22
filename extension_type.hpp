//
// Created by xmh on 01/01/19.
//
#pragma once
#ifdef _MSC_VER
#ifdef ORMPP_ENABLE_MYSQL
#include <include/mysql.h>
#endif
#else
#ifdef ORMPP_ENABLE_MYSQL
#include <mysql/mysql.h>
#endif
#endif
#include <iostream>
#include <string>
#include <ctime>
#include <algorithm>
#include <memory>
#include <sstream>

#ifdef ORMPP_ENABLE_MYSQL
namespace ormpp {
	class DateTime
	{
		friend std::ostream& operator << (std::ostream& os, DateTime const& v);
	public:
		DateTime() :buff_(), is_null_(true)
		{

		}
		DateTime(std::time_t timestamp) :is_null_(false)
		{
			format_from_timestamp(timestamp);
		}
		DateTime(std::string const& datetime)
		{
			if (datetime.empty()) {
				is_null_ = true;
			}
			else {
				is_null_ = false;
			}
			buff_ = datetime;
		}
	public:
		DateTime& operator=(std::time_t timestamp)
		{
			format_from_timestamp(timestamp);
			return *this;
		}
		DateTime& operator=(std::string const& datetime)
		{
			buff_ = datetime;
			is_null_ = false;
			return *this;
		}
		DateTime& format_from_timestamp(std::time_t timestamp)
		{
			auto tm = localtime(&timestamp);
			char buff[255] = { 0 };
			strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", tm);
			buff_.erase();
			std::for_each(buff, &buff[254], [this](char const v) {
				if (v != '\0') {
					buff_.append({ v });
				}
			});
			is_null_ = false;
			return *this;
		}
		std::time_t get_now_timestamp() const
		{
			return std::time(nullptr);
		}

		void set_null() {
			buff_.erase();
			is_null_ = true;
		}

		bool is_null() const {
			return is_null_;
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)(&is_null_);
		}

		std::string& get_value()
		{
			return buff_;
		}

		std::string const& get_value() const
		{
			return buff_;
		}

		std::time_t format_to_timestamp() const
		{
			if (!buff_.empty()) {
				struct tm stm;
				int iY, iM, iD, iH, iMin, iS;
				auto str_time = buff_.data();
				memset(&stm, 0, sizeof(stm));
				iY = atoi(str_time);
				iM = atoi(str_time + 5);
				iD = atoi(str_time + 8);
				iH = atoi(str_time + 11);
				iMin = atoi(str_time + 14);
				iS = atoi(str_time + 17);

				stm.tm_year = iY - 1900;
				stm.tm_mon = iM - 1;
				stm.tm_mday = iD;
				stm.tm_hour = iH;
				stm.tm_min = iMin;
				stm.tm_sec = iS;

				return mktime(&stm);
			}
			return 0;
		}

		char* to_buffer() const
		{
			if (!buff_.empty()) {
				auto str_time = buff_.data();
				int iY, iM, iD, iH, iMin, iS;
				iY = atoi(str_time);
				iM = atoi(str_time + 5);
				iD = atoi(str_time + 8);
				iH = atoi(str_time + 11);
				iMin = atoi(str_time + 14);
				iS = atoi(str_time + 17);
				sql_time.year = iY;
				sql_time.month = iM;
				sql_time.day = iD;
				sql_time.hour = iH;
				sql_time.minute = iMin;
				sql_time.second = iS;
				sql_time.second_part = 0;
			}
			return (char*)(&sql_time);
		}
		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}

		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return buff_;
			}
		}

		void sql_set_buff(std::string&& tmp)
		{
			buff_ = std::move(tmp);
		}
	private:
		std::string buff_;
		mutable MYSQL_TIME sql_time;
		bool is_null_;
	};

	inline std::ostream& operator << (std::ostream& os, DateTime const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << v.buff_;
		}
		return os;
	}


	class SQLDate
	{
		friend std::ostream& operator << (std::ostream& os, SQLDate const& v);
	public:
		SQLDate() :buff_(), is_null_(true)
		{

		}
		SQLDate(std::time_t timestamp) :is_null_(false)
		{
			format_from_timestamp(timestamp);
		}
		SQLDate(std::string const& datet)
		{
			if (datet.empty()) {
				is_null_ = true;
			}
			else {
				is_null_ = false;
			}
			buff_ = datet;
		}
	public:
		SQLDate& operator=(std::time_t timestamp)
		{
			format_from_timestamp(timestamp);
			return *this;
		}
		SQLDate& operator=(std::string const& date)
		{
			buff_ = date;
			is_null_ = false;
			return *this;
		}
		SQLDate& format_from_timestamp(std::time_t timestamp)
		{
			auto tm = localtime(&timestamp);
			char buff[255] = { 0 };
			strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", tm);
			std::string str(buff, sizeof(buff));
			buff_ = str.substr(0, str.find(' '));
			is_null_ = false;
			return *this;
		}

		void set_null() {
			buff_.erase();
			is_null_ = true;
		}

		bool is_null() const{
			return is_null_;
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)(&is_null_);
		}

		std::string& get_value()
		{
			return buff_;
		}

		std::string const& get_value() const
		{
			return buff_;
		}

		char* to_buffer() const
		{
			if (!buff_.empty()) {
				auto str_time = buff_.data();
				int iY, iM, iD;
				iY = atoi(str_time);
				iM = atoi(str_time + 5);
				iD = atoi(str_time + 8);
				//iH = atoi(str_time + 11);
				//iMin = atoi(str_time + 14);
				//iS = atoi(str_time + 17);
				sql_time.year = iY;
				sql_time.month = iM;
				sql_time.day = iD;
				sql_time.hour = 0;
				sql_time.minute = 0;
				sql_time.second = 0;
				sql_time.second_part = 0;
			}
			return (char*)(&sql_time);
		}
		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}
		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return buff_;
			}
		}
		void sql_set_buff(std::string&& tmp)
		{
			buff_ = std::move(tmp);
		}
	private:
		std::string buff_;
		mutable MYSQL_TIME sql_time;
		bool is_null_;
	};

	inline std::ostream& operator << (std::ostream& os, SQLDate const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << v.buff_;
		}
		return os;
	}


	class SQLTime
	{
		friend std::ostream& operator << (std::ostream& os, SQLTime const& v);
	public:
		SQLTime() :buff_(), is_null_(true)
		{

		}
		SQLTime(std::time_t timestamp) :is_null_(false)
		{
			format_from_timestamp(timestamp);
		}
		SQLTime(std::string const& time)
		{
			if (time.empty()) {
				is_null_ = true;
			}
			else {
				is_null_ = false;
			}
			buff_ = time;
		}
	public:
		SQLTime& operator=(std::time_t timestamp)
		{
			format_from_timestamp(timestamp);
			return *this;
		}
		SQLTime& operator=(std::string const& date)
		{
			buff_ = date;
			is_null_ = false;
			return *this;
		}
		SQLTime& format_from_timestamp(std::time_t timestamp)
		{
			auto tm = localtime(&timestamp);
			char buff[255] = { 0 };
			strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", tm);
			std::string str(buff, sizeof(buff));
			buff_ = std::string(buff, sizeof(buff));
			is_null_ = false;
			return *this;
		}

		void set_null() {
			buff_.erase();
			is_null_ = true;
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)(&is_null_);
		}

		bool is_null() const{
			return is_null_;
		}

		std::string& get_value()
		{
			return buff_;
		}

		std::string const& get_value() const
		{
			return buff_;
		}

		char* to_buffer() const
		{
			if (!buff_.empty()) {
				auto str_time = buff_.data();
				//int iY, iM, iD,
				int iH, iMin, iS;
				//iY = atoi(str_time);
				//iM = atoi(str_time + 5);
				//iD = atoi(str_time + 8);

				iH = atoi(str_time + 11);
				iMin = atoi(str_time + 14);
				iS = atoi(str_time + 17);
				sql_time.year = 0;
				sql_time.month = 0;
				sql_time.day = 0;
				sql_time.hour = iH;
				sql_time.minute = iMin;
				sql_time.second = iS;
				sql_time.second_part = 0;
				sql_time.neg = 0;
			}
			return (char*)(&sql_time);
		}
		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}

		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return buff_;
			}
		}

		void sql_set_buff(std::string&& tmp)
		{
			buff_ = std::move(tmp);
		}
	private:
		std::string buff_;
		mutable MYSQL_TIME sql_time;
		bool is_null_;
	};

	inline std::ostream& operator << (std::ostream& os, SQLTime const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << v.buff_;
		}
		return os;
	}

	class Integer
	{
		friend std::ostream& operator << (std::ostream& os, Integer const& v);
	public:
		Integer() : is_null_(true), value_(0)
		{

		}
		Integer(int value) :is_null_(false), value_(value)
		{

		}
		Integer(std::string const& str)
		{
			from_string(str);
		}
	public:
		Integer& operator=(int value)
		{
			value_ = value;
			is_null_ = false;
			return *this;
		}
		Integer& operator=(std::string const& str)
		{
			from_string(str);
			return *this;
		}
		Integer& from_string(std::string const& str)
		{
			char* p = nullptr;
			auto value = strtol(str.c_str(), &p, 10);
			if (strcmp(p, "") == 0) {
				value_ = value;
				is_null_ = false;
			}
			else {
				value_ = 0;
				is_null_ = true;
			}
			return *this;
		}

		int& get_value()
		{
			return value_;
		}

		int const& get_value() const
		{
			return value_;
		}

		void set_null() {
			value_ = 0;
			is_null_ = true;
		}

		bool is_null() const{
			return is_null_;
		}

		void* to_buffer() const
		{
			return const_cast<void*>(static_cast<const void*>(&value_));
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)&is_null_;
		}

		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}
		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return std::to_string(value_);
			}
		}
	private:
		bool is_null_;
		int value_;
	};

	inline  std::ostream& operator << (std::ostream& os, Integer const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << v.value_;
		}
		return os;
	}

	class TinyInt
	{
		friend std::ostream& operator << (std::ostream& os, TinyInt const& v);
	public:
		TinyInt() : is_null_(true), value_(0)
		{

		}
		TinyInt(int value)
		{
			operator=(value);
		}
		TinyInt(std::string const& str)
		{
			from_string(str);
		}
	public:
		TinyInt& operator=(int value)
		{
			if (value >= -128 && value <= 127) {
				value_ = value;
				is_null_ = false;
			}
			else {
				value_ = 0;
				is_null_ = true;
			}
			return *this;
		}
		TinyInt& operator=(std::string const& str)
		{
			from_string(str);
			return *this;
		}
		TinyInt& from_string(std::string const& str)
		{
			char* p = nullptr;
			auto value = strtol(str.c_str(), &p, 10);
			if (strcmp(p, "") == 0) {
				if (value >= -128 && value <= 127) {
					value_ = value;
					is_null_ = false;
				}
				else {
					value_ = 0;
					is_null_ = true;
				}
			}
			else {
				value_ = 0;
				is_null_ = true;
			}
			return *this;
		}

		void set_null() {
			value_ = 0;
			is_null_ = true;
		}

		char& get_value()
		{
			return value_;
		}

		char const& get_value() const
		{
			return value_;
		}

		void* to_buffer() const
		{
			return const_cast<void*>(static_cast<const void*>(&value_));
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)&is_null_;
		}

		bool is_null() const{
			return is_null_;
		}

		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}

		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return std::to_string(value_);
			}
		}
	private:
		bool is_null_;
		char value_;
	};

	inline std::ostream& operator << (std::ostream& os, TinyInt const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << int(v.value_);
		}
		return os;
	}

	class SmallInt
	{
		friend std::ostream& operator << (std::ostream& os, SmallInt const& v);
	public:
		SmallInt() : is_null_(true), value_(0)
		{

		}
		SmallInt(int value)
		{
			operator=(value);
		}
		SmallInt(std::string const& str)
		{
			from_string(str);
		}
	public:
		SmallInt& operator=(int value)
		{
			if (value >= -32768 && value <= 32767) {
				value_ = value;
				is_null_ = false;
			}
			else {
				value_ = 0;
				is_null_ = true;
			}
			return *this;
		}
		SmallInt& operator=(std::string const& str)
		{
			from_string(str);
			return *this;
		}
		SmallInt& from_string(std::string const& str)
		{
			char* p = nullptr;
			auto value = strtol(str.c_str(), &p, 10);
			if (strcmp(p, "") == 0) {
				if (value >= -32768 && value <= 32767) {
					value_ = value;
					is_null_ = false;
				}
				else {
					value_ = 0;
					is_null_ = true;
				}
			}
			else {
				value_ = 0;
				is_null_ = true;
			}
			return *this;
		}

		void set_null() {
			value_ = 0;
			is_null_ = true;
		}

		short& get_value()
		{
			return value_;
		}

		short const& get_value() const
		{
			return value_;
		}

		void* to_buffer() const
		{
			return const_cast<void*>(static_cast<const void*>(&value_));
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)&is_null_;
		}

		bool is_null() const{
			return is_null_;
		}

		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}

		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return std::to_string(value_);
			}
		}
	private:
		bool is_null_;
		short value_;
	};

	inline std::ostream& operator << (std::ostream& os, SmallInt const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << v.value_;
		}
		return os;
	}

	class BigInt
	{
		friend std::ostream& operator << (std::ostream& os, BigInt const& v);
	public:
		BigInt() : is_null_(true), value_(0)
		{

		}
		BigInt(std::int64_t value) :is_null_(false), value_(value)
		{

		}
		BigInt(std::string const& str)
		{
			from_string(str);
		}
	public:
		BigInt& operator=(std::int64_t value)
		{
			value_ = value;
			is_null_ = false;
			return *this;
		}
		BigInt& operator=(std::string const& str)
		{
			from_string(str);
			return *this;
		}
		BigInt& from_string(std::string const& str)
		{
			char* p = nullptr;
			auto value = strtoll(str.c_str(), &p, 10);
			if (strcmp(p, "") == 0) {
				value_ = value;
				is_null_ = false;
			}
			else {
				value_ = 0;
				is_null_ = true;
			}
			return *this;
		}

		void set_null() {
			value_ = 0;
			is_null_ = true;
		}

		std::int64_t& get_value()
		{
			return value_;
		}

		std::int64_t const& get_value() const
		{
			return value_;
		}

		bool is_null() const{
			return is_null_;
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)&is_null_;
		}

		void* to_buffer() const
		{
			return const_cast<void*>(static_cast<const void*>(&value_));
		}

		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}

		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return std::to_string(value_);
			}
		}
	private:
		bool is_null_;
		std::int64_t value_;
	};

	inline  std::ostream& operator << (std::ostream& os, BigInt const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << v.value_;
		}
		return os;
	}

	class Float
	{
		friend std::ostream& operator << (std::ostream& os, Float const& v);
	public:
		Float() : is_null_(true), value_(0)
		{

		}
		Float(float value) :is_null_(false), value_(value)
		{

		}
		Float(std::string const& str)
		{
			from_string(str);
		}
	public:
		Float& operator=(float value)
		{
			value_ = value;
			is_null_ = false;
			return *this;
		}
		Float& operator=(std::string const& str)
		{
			from_string(str);
			return *this;
		}
		Float& from_string(std::string const& str)
		{
			char* p = nullptr;
			auto value = strtof(str.c_str(), &p);
			if (strcmp(p, "") == 0) {
				value_ = value;
				is_null_ = false;
			}
			else {
				value_ = 0.f;
				is_null_ = true;
			}
			return *this;
		}

		void set_null() {
			value_ = 0.f;
			is_null_ = true;
		}

		float& get_value()
		{
			return value_;
		}

		float const& get_value() const
		{
			return value_;
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)&is_null_;
		}

		bool is_null() const{
			return is_null_;
		}

		void* to_buffer() const
		{
			return const_cast<void*>(static_cast<const void*>(&value_));
		}

		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}

		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return std::to_string(value_);
			}
		}
	private:
		bool is_null_;
		float value_;
	};

	inline std::ostream& operator << (std::ostream& os, Float const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << v.value_;
		}
		return os;
	}

	class Double
	{
		friend std::ostream& operator << (std::ostream& os, Double const& v);
	public:
		Double() : is_null_(true), value_(0)
		{

		}
		Double(double value) :is_null_(false), value_(value)
		{

		}
		Double(std::string const& str)
		{
			from_string(str);
		}
	public:
		Double& operator=(double value)
		{
			value_ = value;
			is_null_ = false;
			return *this;
		}
		Double& operator=(std::string const& str)
		{
			from_string(str);
			return *this;
		}
		Double& from_string(std::string const& str)
		{
			char* p = nullptr;
			auto value = strtod(str.c_str(), &p);
			if (strcmp(p, "") == 0) {
				value_ = value;
				is_null_ = false;
			}
			else {
				value_ = 0.f;
				is_null_ = true;
			}
			return *this;
		}

		void set_null() {
			value_ = 0.f;
			is_null_ = true;
		}

		double& get_value()
		{
			return value_;
		}

		double const& get_value() const
		{
			return value_;
		}

		my_bool* sql_set_null()
		{
			return (my_bool*)&is_null_;
		}

		bool is_null() const{
			return is_null_;
		}

		void* to_buffer() const
		{
			return const_cast<void*>(static_cast<const void*>(&value_));
		}

		my_bool* is_sql_null() const
		{
			return (my_bool*)(&is_null_);
		}

		std::string to_string()
		{
			if (is_null_) {
				return "null";
			}
			else {
				return std::to_string(value_);
			}
		}
	private:
		bool is_null_;
		double value_;
	};

	inline  std::ostream& operator << (std::ostream& os, Double const& v)
	{
		if (v.is_null_) {
			os << "Null";
		}
		else {
			os << v.value_;
		}
		return os;
	}
}
#endif
