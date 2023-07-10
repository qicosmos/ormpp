#ifndef ORM_LOGGER_HPP
#define ORM_LOGGER_HPP

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

namespace ormpp {

        enum class LogLevel
        {
            Trace = 0,
            Info,
            Warning,
            Error,
            Fatal,
        };

        class ILogHandler
        {
        public:
            virtual ~ILogHandler() = default;

            virtual void log(std::string message, LogLevel level) = 0;
        };

        class CoutLogHandler : public ILogHandler
        {
        public:
            void log(std::string message, LogLevel level) override
            {
                std::string prefix;
                switch (level)
                {
#define xx(lvl,msg) case lvl: {\
				    prefix = msg;     \
					break; }

                    xx(LogLevel::Trace,"Trace")
                    xx(LogLevel::Info,"Info ")
                    xx(LogLevel::Warning,"Warn ")
                    xx(LogLevel::Error,"Error")
                    xx(LogLevel::Fatal,"Fatal")
#undef xx
                }
                std::cout << std::string("[") + timestamp() + std::string("] [") + prefix + std::string("] ") + message << std::endl;
            }

        private:
            static std::string timestamp()
            {
                char date[32];
                time_t t = time(0);

                tm my_tm;

#if defined(_MSC_VER) || defined(__MINGW32__)
#ifdef ORMPP_USE_LOCALTIMEZONE
                localtime_s(&my_tm, &t);
#else
                gmtime_s(&my_tm, &t);
#endif
#else
#ifdef ORMPP_USE_LOCALTIMEZONE
                localtime_r(&t, &my_tm);
#else
                gmtime_r(&t, &my_tm);
#endif
#endif

                size_t sz = strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &my_tm);
                return std::string(date, date + sz);
            }
        };

        class logger
        {
        public:
            logger(LogLevel level) :
                level_(level)
            {}
            ~logger()
            {
#ifdef ORMPP_ENABLE_LOG
                if (level_ >= get_current_log_level())
                {
                    get_handler_ref()->log(stringstream_.str(), level_);
                }
#endif
            }

            //
            template<typename T>
            logger& operator<<(T const& value)
            {
#ifdef ORMPP_ENABLE_LOG
                if (level_ >= get_current_log_level())
                {
                    stringstream_ << value;
                }
#endif
                return *this;
            }

            logger& operator<<(std::ostream& (*value)(std::ostream&))
            {
#ifdef ORMPP_ENABLE_LOG
                if (level_ >= get_current_log_level())
                {
                    stringstream_ << value;
                }
#endif
                return *this;
            }


            //
            static void setLogLevel(LogLevel level) { get_log_level_ref() = level; }

            static void setHandler(ILogHandler* handler) { get_handler_ref() = handler; }

            static LogLevel get_current_log_level() { return get_log_level_ref(); }

        private:
            //
            static LogLevel& get_log_level_ref()
            {
                static LogLevel current_level = static_cast<LogLevel>(0);
                return current_level;
            }
            static ILogHandler*& get_handler_ref()
            {
                static CoutLogHandler default_handler;
                static ILogHandler* current_handler = &default_handler;
                return current_handler;
            }

            //
            std::ostringstream stringstream_;
            LogLevel level_;
        };


#define ORMPP_LOG_FATAL                                                  \
    if (ormpp::logger::get_current_log_level() <= ormpp::LogLevel::Fatal) \
    ormpp::logger(ormpp::LogLevel::Fatal)
#define ORMPP_LOG_ERROR                                                  \
    if (ormpp::logger::get_current_log_level() <= ormpp::LogLevel::Error) \
    ormpp::logger(ormpp::LogLevel::Error)
#define ORMPP_LOG_WARN                                                  \
    if (ormpp::logger::get_current_log_level() <= ormpp::LogLevel::Warn) \
    ormpp::logger(ormpp::LogLevel::Warn)
#define ORMPP_LOG_INFO                                                  \
    if (ormpp::logger::get_current_log_level() <= ormpp::LogLevel::Info) \
    ormpp::logger(ormpp::LogLevel::Info)
#define ORMPP_LOG_TRACE                                                  \
    if (ormpp::logger::get_current_log_level() <= ormpp::LogLevel::Trace) \
    ormpp::logger(ormpp::LogLevel::Trace)



}// namespace ormpp




#endif  // ORM_LOGGER_HPP