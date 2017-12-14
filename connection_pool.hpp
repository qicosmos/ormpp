//
// Created by qiyu on 12/14/17.
//

#ifndef ORMPP_CONNECTION_POOL_HPP
#define ORMPP_CONNECTION_POOL_HPP

#include <deque>
#include <memory>
#include <mutex>
#include <chrono>
#include <condition_variable>

namespace ormpp{
    template<typename DB>
    class connection_pool{
    public:
        static connection_pool<DB>& instance(){
            static connection_pool<DB> instance;
            return instance;
        }

        //call_once
        template<typename... Args>
        void init(int maxsize, Args&&... args){
            std::call_once(flag_, &connection_pool<DB>::init_impl<Args...>, this, maxsize, std::forward<Args>(args)...);
        }

        std::shared_ptr<DB> get(){
            std::unique_lock lock( mutex_ );

            while ( pool_.empty() ){
                if(condition_.wait_for(lock, std::chrono::seconds(10))== std::cv_status::timeout){
                    //timeout
                    return nullptr;
                }
            }

            auto conn_ = pool_.front();
            pool_.pop_front();
            return conn_;
        }

        void return_back(std::shared_ptr<DB> conn){
            std::unique_lock lock( mutex_ );
            pool_.push_back( conn );
            lock.unlock();
            condition_.notify_one();
        }
    private:
        template<typename... Args>
        void init_impl(int maxsize, Args&&... args){
            for (int i = 0; i < maxsize; ++i) {
                auto conn = std::make_shared<DB>();
                if(conn->connect(std::forward<Args>(args)...)){
                    pool_.push_back(conn);
                }
                else{
                    throw std::invalid_argument("init failed");
                }
            }
        }

        //todo: check idle connection, the max idle time should less than 8 hours

        connection_pool()= default;
        ~connection_pool()= default;
        connection_pool(const connection_pool&)= delete;
        connection_pool& operator=(const connection_pool&)= delete;

        std::deque<std::shared_ptr<DB>> pool_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::once_flag flag_;
    };

    template<typename DB>
    struct conn_guard{
        conn_guard(std::shared_ptr<DB> con) : conn_(con){}
        ~conn_guard(){
            if(conn_!= nullptr)
                connection_pool<DB>::instance().return_back(conn_);
        }
    private:
        std::shared_ptr<DB> conn_;
    };
}


#endif //ORMPP_CONNECTION_POOL_HPP
