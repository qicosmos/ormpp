//
// Created by qiyu on 10/30/17.
//

#ifndef ORM_POSTGRESQL_HPP
#define ORM_POSTGRESQL_HPP

#include <string>
#include <type_traits>
#ifdef _MSC_VER
#include <include/libpq-fe.h>
#else
#include <postgresql/libpq-fe.h>
#endif

using namespace std::string_literals;

namespace ormpp{
    class postgresql{
    public:
        ~postgresql(){
            disconnect();
        }

        //ip, user, pwd, db, timeout  the sequence must be fixed like this
        template <typename... Args>
        bool connect(Args&&... args){
            auto sql = ""s;
			sql = generate_conn_sql(std::make_tuple(std::forward<Args>(args)...));
  
            con_ = PQconnectdb(sql.data());
            if (PQstatus(con_) != CONNECTION_OK){
                std::cout<<PQerrorMessage(con_)<<std::endl;
                return false;
            }

            return true;
        }

        template <typename... Args>
        bool disconnect(Args&&... args){
            if(con_!= nullptr){
                PQfinish(con_);
                con_ = nullptr;
            }

            return true;
        }

		bool ping() {
			return (PQstatus(con_) == CONNECTION_OK);
		}

		bool has_error() {
			return false;//todo
		}

        template<typename T, typename... Args>
        constexpr auto create_datatable(Args&&... args){
//            std::string droptb = "DROP TABLE IF EXISTS ";
//            droptb += iguana::get_name<T>();
//
//            res_ = PQexec(con_, droptb.data());
//            if (PQresultStatus(res_) != PGRES_COMMAND_OK){
//                PQclear(res_);
//                return false;
//            }
//
//            PQclear(res_);

            std::string sql = generate_createtb_sql<T>(std::forward<Args>(args)...);
            res_ = PQexec(con_, sql.data());
            if (PQresultStatus(res_)!=PGRES_COMMAND_OK) {
                std::cout<<PQerrorMessage(con_)<<std::endl;
                PQclear(res_);
                return false;
            }
            PQclear(res_);

            return true;
        }

        template<typename T, typename... Args>
        constexpr int insert(const T& t,Args&&... args){
//            std::string sql = generate_pq_insert_sql<T>(false);
            std::string sql = generate_auto_insert_sql<T>(false);
            if(!prepare<T>(sql))
                return INT_MIN;

            return insert_impl(sql, t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int insert(const std::vector<T>& v,Args&&... args){
//            std::string sql = generate_pq_insert_sql<T>(false);
            std::string sql = generate_auto_insert_sql<T>(false);

            if(!begin())
                return INT_MIN;

            if(!prepare<T>(sql))
                return INT_MIN;

            for(auto& item : v){
                auto result = insert_impl(sql, item, std::forward<Args>(args)...);
                if(result==INT_MIN){
                    rollback();
                    return INT_MIN;
                }
            }

            if(!commit())
                return INT_MIN;

            return (int)v.size();
        }

        //if there is no key in a table, you can set some fields as a condition in the args...
        template<typename T, typename... Args>
        constexpr int update(const T& t, Args&&... args) {
            //transaction, firstly delete, secondly insert
            auto name = iguana::get_name<T>();
            auto it = key_map_.find(name.data());
            auto key = it==key_map_.end()?"":it->second;

            auto condition = get_condition(t, key, std::forward<Args...>(args)...);
            if(!begin())
                return INT_MIN;

            if(!delete_records<T>(condition)){
                rollback();
                return INT_MIN;
            }

            if(insert(t)<0){
                rollback();
                return INT_MIN;
            }

            if(!commit())
                return INT_MIN;

            return 1;
        }

        template<typename T, typename... Args>
        constexpr int update(const std::vector<T>& v, Args&&... args){
            //transaction, firstly delete, secondly insert
            if(!begin())
                return INT_MIN;

            auto name = iguana::get_name<T>();
            auto it = key_map_.find(name.data());
            auto key = it==key_map_.end()?"":it->second;
            for(auto& t: v){
                auto condition = get_condition(t, key, std::forward<Args...>(args)...);

                if(!delete_records<T>(condition)){
                    rollback();
                    return INT_MIN;
                }

                if(insert(t)<0){
                    rollback();
                    return INT_MIN;
                }
            }

            if(!commit())
                return INT_MIN;

            return (int)v.size();
        }

        template<typename T, typename... Args>
        constexpr std::enable_if_t<iguana::is_reflection_v<T>, std::vector<T>> query(Args&&... args){
            std::string sql = generate_query_sql<T>(std::forward<Args>(args)...);
            constexpr auto SIZE = iguana::get_value<T>();

            if(!prepare<T>(sql))
                return {};

            res_ = PQexec(con_, sql.data());
            if (PQresultStatus(res_) != PGRES_TUPLES_OK){
                PQclear(res_);
                return {};
            }

            std::vector<T> v;
            auto ntuples = PQntuples(res_);

            for(auto i = 0; i < ntuples; i++){
                T t = {};
                iguana::for_each(t, [this, i, &t](auto item, auto I)
                {
                    assign(t.*item, i, (int)decltype(I)::value);
                });
                v.push_back(std::move(t));
            }

            PQclear(res_);

            return v;
        }

        template<typename T, typename Arg, typename... Args>
        constexpr std::enable_if_t<!iguana::is_reflection_v<T>, std::vector<T>> query(const Arg& s, Args&&... args){
            static_assert(iguana::is_tuple<T>::value);
            constexpr auto SIZE = std::tuple_size_v<T>;

            std::string sql = s;
            constexpr auto Args_Size = sizeof...(Args);
            if(Args_Size!=0){
                if(Args_Size!=std::count(sql.begin(), sql.end(), '$'))
                    return {};

                sql = get_sql(sql, std::forward<Args>(args)...);
            }

            if(!prepare<T>(sql))
                return {};

            res_ = PQexec(con_, sql.data());
            if (PQresultStatus(res_) != PGRES_TUPLES_OK){
                PQclear(res_);
                return {};
            }

            std::vector<T> v;
            auto ntuples = PQntuples(res_);

            for(auto i = 0; i < ntuples; i++){
                T tp = {};
                int index = 0;
                iguana::for_each(tp, [this, i, &index](auto& item, auto I)
                {
                    if constexpr(iguana::is_reflection_v<decltype(item)>){
                    std::remove_reference_t<decltype(item)> t = {};
                    iguana::for_each(t, [this, &index, &t](auto ele, auto i)
                    {
                        assign(t.*ele, (int)i, index++);
                    });
                    item = std::move(t);
                    }else{
                        assign(item, (int)i, index++);
                    }
                }, std::make_index_sequence<SIZE>{});
                v.push_back(std::move(tp));
            }

            PQclear(res_);

            return v;
        }

        template<typename T, typename... Args>
        constexpr bool delete_records(Args&&... where_conditon){
            auto sql = generate_delete_sql<T>(std::forward<Args>(where_conditon)...);
            res_ = PQexec(con_, sql.data());
            if (PQresultStatus(res_)!=PGRES_COMMAND_OK) {
                PQclear(res_);
                return false;
            }
            PQclear(res_);

            return true;
        }

        //just support execute string sql without placeholders
        auto execute(const std::string& sql){
            res_ = PQexec(con_, sql.data());
            auto guard = guard_result(res_);
            if (PQresultStatus(res_) != PGRES_COMMAND_OK){
//                std::cout<<PQerrorMessage(con_)<<std::endl;
                return false;
            }

            return true;
        }

        //transaction
        bool begin(){
            res_ = PQexec(con_, "begin;");
            auto guard = guard_result(res_);
            if (PQresultStatus(res_) != PGRES_COMMAND_OK){
//                std::cout<<PQerrorMessage(con_)<<std::endl;
                return false;
            }

            return true;
        }

        bool commit(){
            res_ = PQexec(con_, "commit;");
            auto guard = guard_result(res_);
            if (PQresultStatus(res_) != PGRES_COMMAND_OK){
//                std::cout<<PQerrorMessage(con_)<<std::endl;
                return false;
            }

            return true;
        }

        bool rollback(){
            res_ = PQexec(con_, "rollback;");
            auto guard = guard_result(res_);
            if (PQresultStatus(res_) != PGRES_COMMAND_OK){
//                std::cout<<PQerrorMessage(con_)<<std::endl;
                return false;
            }

            return true;
        }

    private:
        template<typename T>
        auto to_str(T&& t){
            if constexpr(std::is_integral_v<std::decay_t <T>>)
                return std::to_string(std::forward<T>(t));
            else
                return t;
        }

		template<typename Tuple>
		std::string generate_conn_sql(const Tuple& tp) {
			constexpr size_t SIZE = std::tuple_size_v<Tuple>;
			if constexpr (SIZE == 4) {
				return generate_conn_sql(std::make_tuple("host", "user", "password", "dbname"),
					tp, std::make_index_sequence<SIZE>{});
			}
			else if constexpr (SIZE == 5) {
				return generate_conn_sql(std::make_tuple("host", "user", "password", "dbname", "connect_timeout"),
					tp, std::make_index_sequence<SIZE>{});
			}
			else {
				return "";
			}
		}

        template<typename... Args1, typename... Args2, std::size_t... Idx>
        std::string generate_conn_sql(const std::tuple<Args1...>& tp1, const std::tuple<Args2...>& tp2, std::index_sequence<Idx...>){
            std::string sql = "";
            (append(sql, std::get<Idx>(tp1), "=", to_str(std::get<Idx>(tp2)), " "), ...);
            return sql;
        }

        struct guard_result{
            guard_result(PGresult* res):res_(res){}
            void dismiss(){
                dismiss_ = true;
            }

            ~guard_result(){
                if(dismiss_)
                    return;

                if(res_!= nullptr)
                    status_ = PQresultStatus(res_);

                if (status_ != PGRES_COMMAND_OK)
                    std::cout<<PQresultErrorMessage(res_)<<std::endl;
            }

        private:
            PGresult* res_= nullptr;
            int status_ = 0;
            bool dismiss_ = false;
        };

        template<typename T, typename... Args >
        std::string generate_createtb_sql(Args&&... args)
        {
            const auto type_name_arr = get_type_names<T>(DBType::postgresql);
            constexpr auto name = iguana::get_name<T>();
            std::string sql = std::string("CREATE TABLE IF NOT EXISTS ") + name.data()+"(";
            auto arr = iguana::get_array<T>();
            constexpr const size_t SIZE = sizeof... (Args);
            auto_key_map_[name.data()] = "";
            key_map_[name.data()] = "";

            //auto_increment_key and key can't exist at the same time			
			using U = std::tuple<std::decay_t <Args>...>;
            if constexpr (SIZE>0){
				//using U = std::tuple<std::decay_t <Args>...>; //the code can't compile in vs2017, why?maybe args... in if constexpr?
                static_assert(!(iguana::has_type<ormpp_key, U>::value&&iguana::has_type<ormpp_auto_key, U>::value), "should only one key");
            }

            //at first sort the args, make sure the key always in the head
            auto tp = sort_tuple(std::make_tuple(std::forward<Args>(args)...));
            const size_t arr_size = arr.size();
            for(size_t i=0; i< arr_size; ++i) {
                auto field_name = arr[i];
                bool has_add_field = false;
                iguana::for_each(tp, [&sql, &i, &has_add_field, field_name, type_name_arr,name, this](auto item, auto I){
                    if constexpr (std::is_same_v<decltype(item), ormpp_not_null>){
                    if(item.fields.find(field_name.data())==item.fields.end())
                        return;
                }
                    else{
                    if(item.fields != field_name.data())
                        return;
                }

                    if constexpr (std::is_same_v<decltype(item), ormpp_not_null>){
                    if(!has_add_field){
                        append(sql, field_name.data(), " ", type_name_arr[i]);
                        has_add_field = true;
                    }

                    append(sql, " NOT NULL");
                }
                    else if constexpr (std::is_same_v<decltype(item), ormpp_key>){
                    if(!has_add_field){
                        append(sql, field_name.data(), " ", type_name_arr[i]);
                        has_add_field = true;
                    }
                    append(sql, " PRIMARY KEY ");

                    key_map_[name.data()] = item.fields;
                }
                    else if constexpr (std::is_same_v<decltype(item), ormpp_auto_key>){
                    if(!has_add_field){
                        append(sql, field_name.data(), " ");
                        has_add_field = true;
                    }
                    append(sql, " serial primary key");
                    auto_key_map_[name.data()] = item.fields;
                    key_map_[name.data()] = item.fields;
                }
					else if constexpr (std::is_same_v<decltype(item), ormpp_unique>) {
						if (!has_add_field) {
							append(sql, field_name.data(), " ", type_name_arr[i]);
						}

						append(sql, ", UNIQUE(", item.fields, ")");
						has_add_field = true;
					}
                    else {
                    append(sql, field_name.data(), " ", type_name_arr[i]);
                }
                }, std::make_index_sequence<SIZE>{});

                if(!has_add_field){
                    append(sql, field_name.data(), " ", type_name_arr[i]);
                }

                if(i<arr_size-1)
                    sql+=", ";
            }

            sql += ")";

            return sql;
        }

        template<typename T>
        bool prepare(const std::string& sql){
            res_ = PQprepare(con_, "", sql.data(), (int)iguana::get_value<T>(), nullptr);
            if (PQresultStatus(res_) != PGRES_COMMAND_OK){
                std::cout<<PQresultErrorMessage(res_)<<std::endl;
                PQclear(res_);
                return false;
            }
            PQclear(res_);

            return true;
        }

        template<typename T>
        std::string generate_pq_insert_sql(bool replace){
            std::string sql = replace?"replace into ":"insert into ";
            constexpr auto SIZE = iguana::get_value<T>();
            constexpr auto name = iguana::get_name<T>();
            append(sql, name.data(), " values(");
            char temp[20] = {};
            for (auto i = 0; i < SIZE; ++i) {
                sql+="$";
                itoa_fwd(i+1, temp);
                sql+=temp;
                if(i<SIZE-1)
                    sql+=", ";
                else
                    sql+=");";
            }

            return sql;
        }

        template<typename T, typename... Args>
        constexpr int insert_impl(const std::string& sql, const T& t, Args&&... args) {
            std::vector<std::vector<char>> param_values;
            auto it = auto_key_map_.find(iguana::get_name<T>().data());
            std::string auto_key = (it==auto_key_map_.end())?"":it->second;

            iguana::for_each(t, [&t, &param_values, &auto_key, this](auto item, auto i){
                /*if(!auto_key.empty()&&auto_key==iguana::get_name<T>(decltype(i)::value).data())
                    return;*/
                set_param_values(param_values, t.*item);
            });

            if(param_values.empty())
                return INT_MIN;

            std::vector<const char*> param_values_buf;
            for(auto& item : param_values){
                param_values_buf.push_back(item.data());
            }

            res_ = PQexecPrepared(con_, "", (int)param_values.size(), param_values_buf.data(), NULL,NULL,0);

            if (PQresultStatus(res_) != PGRES_COMMAND_OK){
                std::cout<<PQresultErrorMessage(res_)<<std::endl;
                PQclear(res_);
                return INT_MIN;
            }

            PQclear(res_);

            return 1;
        }

        template<typename T>
        constexpr void set_param_values(std::vector<std::vector<char>>& param_values, T&& value){
            using U = std::remove_const_t<std::remove_reference_t<T>>;
            if constexpr(std::is_integral_v<U>&&!iguana::is_int64_v<U>){
                std::vector<char> temp(20, 0);
                itoa_fwd(value, temp.data());
                param_values.push_back(std::move(temp));
            }
            else if constexpr (iguana::is_int64_v<U>){
                std::vector<char> temp(65, 0);
                xtoa(value, temp.data(), 10, std::is_signed_v<U>);
                param_values.push_back(std::move(temp));
            }
            else if constexpr (std::is_floating_point_v<U>){
                std::vector<char> temp(20, 0);
                sprintf(temp.data(), "%f", value);
                param_values.push_back(std::move(temp));
            }
            else if constexpr(std::is_same_v<std::string, U>){
                std::vector<char> temp = {};
                std::copy(value.data(), value.data()+value.size()+1, std::back_inserter(temp));
//                    std::cout<<value.size()<<std::endl;
                param_values.push_back(std::move(temp));
            }
			else if constexpr(is_char_array_v<U>) {
				std::vector<char> temp = {};
				std::copy(value, value+sizeof(U), std::back_inserter(temp));
				param_values.push_back(std::move(temp));
			}
            else {
                std::cout<<"this type has not supported yet"<<std::endl;
            }
        }

        template<typename T>
        constexpr void assign(T&& value, int row, int i){
            using U = std::remove_const_t<std::remove_reference_t<T>>;
            if constexpr(std::is_integral_v<U>&&!iguana::is_int64_v<U>){
                value = std::atoi(PQgetvalue(res_, row, i));
            }
            else if constexpr (iguana::is_int64_v<U>){
                value = std::atoll(PQgetvalue(res_, row, i));
            }
            else if constexpr (std::is_floating_point_v<U>){
                value = std::atof(PQgetvalue(res_, row, i));
            }
            else if constexpr(std::is_same_v<std::string, U>){
                value = PQgetvalue(res_, row, i);
            }
			else if constexpr(is_char_array_v<U>) {
				auto p = PQgetvalue(res_, row, i);
				memcpy(value, p, sizeof(U));
			}
            else {
                std::cout<<"this type has not supported yet"<<std::endl;
            }
        }

        template <typename T, typename... Args>
        constexpr std::string get_condition(const T& t, const std::string& key, Args&&... args){
            std::string result = "";
            constexpr auto SIZE = iguana::get_value<T>();
            iguana::for_each(t, [&](auto& item, auto i){
                constexpr auto Idx = decltype(i)::value;
                using U = std::remove_reference_t<decltype(iguana::get<Idx>(std::declval<T>()))>;
                if(!key.empty()&&key==iguana::get_name<T, Idx>().data()){
                    if constexpr(std::is_arithmetic_v<U>){
                        append(result, iguana::get_name<T, Idx>().data(), "=", std::to_string(t.*item));
                    }else if constexpr(std::is_same_v<std::string, U>){
                        append(result, iguana::get_name<T, Idx>().data(), "=", t.*item);
                    }
                }

                //if constexpr (sizeof...(Args)>0){
				build_condition_by_key<T, Idx>(result, t.*item, args...);
				//(test(args), ...);
				//	(build_condition_by_key_impl(result, t.*item, args),...); //can't pass U in vs2017
				//}
            });

            return result;
        }

		template<typename T, size_t Idx, typename V, typename... Args>
		void build_condition_by_key(std::string& result, const V& t, Args... args) {
			(build_condition_by_key<T, Idx>(result, t, args), ...);
		}

		template<typename T, size_t Idx, typename V, typename W>
		void build_condition_by_key_impl(std::string& result, const V& val, W& key) {
			using U = std::remove_reference_t<decltype(iguana::get<Idx>(std::declval<T>()))>;
			if (key == iguana::get_name<T, Idx>().data()) {
				if constexpr(std::is_arithmetic_v<U>) {
					append(result, " and ");
					append(result, iguana::get_name<T, Idx>().data(), "=", std::to_string(val));
				}
				else if constexpr(std::is_same_v<std::string, U>) {
					append(result, " and ");
					append(result, iguana::get_name<T, Idx>().data(), "=", val);
				}
			}
		}

        template<typename  T>
        std::string generate_auto_insert_sql(bool replace){
            std::string sql = replace?"replace into ":"insert into ";
            constexpr auto SIZE = iguana::get_value<T>();
            constexpr auto name = iguana::get_name<T>();
            append(sql, name.data());

            std::string fields = "(";
            std::string values = " values(";
            auto it = auto_key_map_.find(name.data());

            int index = 0;
            for (auto i = 0; i < SIZE; ++i) {
                std::string field_name = iguana::get_name<T>(i).data();
                //if(it!=auto_key_map_.end()&&it->second==field_name)
                //    continue;

                values+="$";
                char temp[20] = {};
                itoa_fwd(index+1, temp);
                index++;
                values+=temp;

                fields+=field_name;
                if(i<SIZE-1){
                    fields+=", ";
                    values+=", ";
                }
                else{
                    fields+=")";
                    values+=")";
                }
            }
            append(sql, fields, values);
            return sql;
        }

        PGresult *res_ = nullptr;
        PGconn* con_ = nullptr;
        std::map<std::string, std::string> auto_key_map_;
        std::map<std::string, std::string> key_map_;
    };
}
#endif //ORM_POSTGRESQL_HPP
