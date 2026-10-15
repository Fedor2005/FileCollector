#include "../include/Exception.hpp"

#include <stdexcept>
#include <string>

using namespace fc;

Exception::Exception(ErrorCode code, const std::string &msg)
    : code_(code), std::runtime_error(msg) {}

ErrorCode Exception::code() const noexcept { return code_; }
