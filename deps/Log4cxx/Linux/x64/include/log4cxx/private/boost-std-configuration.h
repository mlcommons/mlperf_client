#ifndef BOOST_STD_CONFIGURATION_H
#define BOOST_STD_CONFIGURATION_H

#define STD_FILESYSTEM_FOUND 1
#define Boost_FILESYSTEM_FOUND 0
#define STD_EXPERIMENTAL_FILESYSTEM_FOUND 1

#if STD_FILESYSTEM_FOUND
#include <filesystem>
namespace log4cxx {
namespace filesystem {
    typedef std::filesystem::path path;
}
}
#elif STD_EXPERIMENTAL_FILESYSTEM_FOUND
#include <experimental/filesystem>
namespace log4cxx {
namespace filesystem {
    typedef std::experimental::filesystem::path path;
}
}
#elif Boost_FILESYSTEM_FOUND
#include <boost/filesystem.hpp>
namespace log4cxx {
namespace filesystem {
    typedef boost::filesystem::path path;
}
}
#endif

#endif /* BOOST_STD_CONFIGURATION_H */
