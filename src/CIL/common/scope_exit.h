#ifndef SCOPE_EXIT_H_
#define SCOPE_EXIT_H_

#include <string>
#include <functional>

namespace cil {
namespace utils {

class ScopeExit {
 public:
  ScopeExit(std::function<void()> callback = nullptr) : callback_(callback) {}
  ~ScopeExit() noexcept {
    if (callback_) callback_();
  }
  void SetCallback(std::function<void()> callback) { callback_ = callback; }

 private:
  std::function<void()> callback_;
};

}  // namespace utils
}  // namespace cil

#endif  // !LOGGER_H_
