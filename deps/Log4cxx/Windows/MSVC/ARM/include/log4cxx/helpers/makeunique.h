#ifndef LOG4CXX_MAKE_UNIQUE_H
#define LOG4CXX_MAKE_UNIQUE_H
#include <memory>

#define STD_MAKE_UNIQUE_FOUND 1

#if !STD_MAKE_UNIQUE_FOUND
namespace std
{
	template <typename T, typename ...Args>
unique_ptr<T> make_unique( Args&& ...args )
{
	return unique_ptr<T>( new T( forward<Args>(args)... ) );
}
} // namespace std
#endif

#endif /* LOG4CXX_MAKE_UNIQUE_H */
