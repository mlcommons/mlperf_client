/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef LOG4CXX_LOG4CXX_H
#define LOG4CXX_LOG4CXX_H

/* GENERATED FILE WARNING!  DO NOT EDIT log4cxx.h
 *
 * Edit log4cxx.h.in instead
 *
 */


#define LOG4CXX_ABI_VERSION 15
#define LOG4CXX_VERSION_MAJOR 1
#define LOG4CXX_VERSION_MINOR 4
#define LOG4CXX_VERSION_PATCH 0
#define LOG4CXX_VERSION_TWEAK 0
#define LOG4CXX_MAKE_VERSION(major, minor, patch, tweak) (((major) << 24) |\
	((minor) << 16) |\
	((patch) << 8) |\
	(tweak) )
#define LOG4CXX_VERSION_GET_MAJOR(version) (((version) >> 24) & 0xFF)
#define LOG4CXX_VERSION_GET_MINOR(version) (((version) >> 16) & 0xFF)
#define LOG4CXX_VERSION_GET_PATCH(version) (((version) >> 8) & 0xFF)
#define LOG4CXX_VERSION_GET_TWEAK(version) ((version) & 0xFF)
#define LOG4CXX_VERSION \
  LOG4CXX_MAKE_VERSION(LOG4CXX_VERSION_MAJOR, LOG4CXX_VERSION_MINOR, LOG4CXX_VERSION_PATCH, LOG4CXX_VERSION_TWEAK)

#define LOG4CXX_LOGCHAR_IS_UNICHAR 0
#define LOG4CXX_LOGCHAR_IS_UTF8 1
#define LOG4CXX_LOGCHAR_IS_WCHAR 0

#define LOG4CXX_CHAR_API 1
#define LOG4CXX_WCHAR_T_API 1
#define LOG4CXX_UNICHAR_API 0
#define LOG4CXX_CFSTRING_API 0
#define LOG4CXX_HAS_NETWORKING 1
#define LOG4CXX_HAS_MULTIPROCESS_ROLLING_FILE_APPENDER 0
#define LOG4CXX_EVENTS_AT_EXIT 0
#define LOG4CXX_HAS_DOMCONFIGURATOR 1
#define LOG4CXX_HAS_FMT_LAYOUT 0


#define LOG4CXX_USE_GLOBAL_SCOPE_TEMPLATE 0
#define LOG4CXX_LOGSTREAM_ADD_NOP 0

#include <log4cxx/helpers/makeunique.h>
#include <cstdint>

#define LOG4CXX_PTR_DEF(T) typedef std::shared_ptr<T> T##Ptr;\
	typedef std::weak_ptr<T> T##WeakPtr
#define LOG4CXX_UNIQUE_PTR_DEF(T) typedef std::unique_ptr<T> T##UniquePtr;
#define LOG4CXX_LIST_DEF(N, T) typedef std::vector<T> N
#define LOG4CXX_PRIVATE_PTR(T) std::unique_ptr<T>

#if defined(_MSC_VER)
#define LOG4CXX_DECLARE_PRIVATE_MEMBER_PTR(T, V) \
__pragma( warning( push ) ) \
__pragma( warning( disable : 4251 ) ) \
    struct T; LOG4CXX_PRIVATE_PTR(T) V; \
__pragma( warning( pop ) )

#define LOG4CXX_DECLARE_PRIVATE_MEMBER(T, V) \
__pragma( warning( push ) ) \
__pragma( warning( disable : 4251 ) ) \
    T V; \
__pragma( warning( pop ) )

#define LOG4CXX_INSTANTIATE_EXPORTED_PTR(T) template class LOG4CXX_EXPORT std::shared_ptr<T>
#else // !defined(_MSC_VER)
#define LOG4CXX_DECLARE_PRIVATE_MEMBER_PTR(T, V) struct T; LOG4CXX_PRIVATE_PTR(T) V;
#define LOG4CXX_DECLARE_PRIVATE_MEMBER(T, V) T V;
#define LOG4CXX_INSTANTIATE_EXPORTED_PTR(T)
#endif // defined(_MSC_VER)

#if defined(_WIN32) && defined(_MSC_VER)
#if defined(LOG4CXX_STATIC)     // Linking a static library?
#define LOG4CXX_EXPORT
#elif defined(LOG4CXX)          // Building a DLL?
#define LOG4CXX_EXPORT __declspec(dllexport)
#else                          // Linking against a DLL?
#define LOG4CXX_EXPORT __declspec(dllimport)
#endif // !LOG4CXX_STATIC
#elif defined(__GNUC__) && 4 <= __GNUC__ && 15 < LOG4CXX_ABI_VERSION
  #define LOG4CXX_EXPORT __attribute__ ((visibility ("default")))
#else // !(defined(_WIN32) && defined(_MSC_VER)) || LOG4CXX_ABI_VERSION <= 15 || __GNUC__ < 4
  #define LOG4CXX_EXPORT
#endif

#define LOG4CXX_NS log4cxx

namespace LOG4CXX_NS {

/**
 * log4cxx_time_t - holds the number of microseconds since 1970-01-01
 */
typedef int64_t log4cxx_time_t;

typedef int log4cxx_status_t;

/**
 * Query the compiled version of the library.  Ideally, this should
 * be the same as the LOG4CXX_VERSION macro defined above.
 *
 * The LOG4CXX_VERSION_GET_ series of macros let you extract the
 * specific bytes of the version if required.
 */
LOG4CXX_EXPORT uint32_t libraryVersion();

}

#if 0
namespace log4cxx = LOG4CXX_NS;
#endif

#define LOG4CXX_USING_STD_FORMAT 0
#if !defined(LOG4CXX_FORMAT_NS) && LOG4CXX_USING_STD_FORMAT
#define LOG4CXX_FORMAT_NS std
#elif !defined(LOG4CXX_FORMAT_NS)
#define LOG4CXX_FORMAT_NS fmt
#endif

#endif
