#ifndef CIL_COMMON_EXPECTED_H_
#define CIL_COMMON_EXPECTED_H_

#include <string>
#include <utility>
#include <variant>

namespace cil {

template <typename T>
class Expected {
 public:
  Expected(T value) : data_(std::move(value)) {}

  static Expected error(std::string msg) {
    return Expected(Error{std::move(msg)});
  }

  explicit operator bool() const { return data_.index() == 0; }
  bool has_value() const { return data_.index() == 0; }

  T& value() & { return std::get<0>(data_); }
  const T& value() const& { return std::get<0>(data_); }
  T&& value() && { return std::get<0>(std::move(data_)); }

  const std::string& error() const { return std::get<1>(data_).message; }

 private:
  struct Error {
    std::string message;
  };
  explicit Expected(Error err) : data_(std::move(err)) {}
  std::variant<T, Error> data_;
};

}  // namespace cil

#endif  // CIL_COMMON_EXPECTED_H_
