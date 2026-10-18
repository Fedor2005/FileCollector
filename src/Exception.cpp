#include "Exception.hpp"

#include <stdexcept>
#include <string>

namespace fc {

Exception::Exception(ErrorCode code, const std::string &msg)
    : std::runtime_error(msg), code_(code) {}

ErrorCode Exception::code() const noexcept { return code_; }
} // namespace fc
