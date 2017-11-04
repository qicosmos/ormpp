//
// Created by qiyu on 11/3/17.
//

#ifndef ORM_DBNG_HPP
#define ORM_DBNG_HPP

#include <string>
#include <string_view>
namespace ormpp{
    template<typename DB>
    class dbng{
    public:
        //ip, user, pwd, db  the sequence must be fixed like this
        template <typename... Args>
        bool connect(Args&&... args){
            return  db_.template connect(std::forward<Args>(args)...);
        }

        template <typename... Args>
        bool disconnect(Args&&... args){
            return  db_.template disconnect(std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr auto create_datatable(Args&&... args){
            return db_.template create_datatable<T>(std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int insert(const T& t,Args&&... args){
            return db_.insert(t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int insert(const std::vector<T>& t, Args&&... args){
            return db_.insert(t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int update(const T& t, Args&&... args) {
            return db_.update(t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr int update(const std::vector<T>& t, Args&&... args){
            return db_.update(t, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        constexpr bool delete_records(Args&&... where_conditon){
            return db_.template delete_records<T>(std::forward<Args>(where_conditon)...);
        }

        //restriction, all the args are string, the first is the where condition, rest are append conditions
        template<typename T, typename... Args>
        constexpr auto query(Args&&... args){
            return db_.template query<T>(std::forward<Args>(args)...);
        }

        auto excecute(const std::string& sql){
            return db_.excecute(sql);
        }

        //transaction
        bool begin(){
            return db_.begin();
        }

        bool commit(){
            return db_.commit();
        }

        bool rollback(){
            return db_.rollback();
        }

    private:
        DB db_;
    };
}

#endif //ORM_DBNG_HPP
