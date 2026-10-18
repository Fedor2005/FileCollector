#include "../include/Exception.hpp"

#include <stdexcept>
#include <string>

using namespace fc;

Exception::Exception(ErrorCode code, const std::string &msg)
    : std::runtime_error(msg), code_(code) {}

ErrorCode Exception::code() const noexcept { return code_; }
