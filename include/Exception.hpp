#pragma once

#include <stdexcept>
#include <string>

namespace fc {

enum class ErrorCode { OK = 0, FileNotExist, FileNotFilled, FileIdAlreadyUse };
class Exception : public std::runtime_error {
  ErrorCode code_;

public:
  Exception(ErrorCode code, const std::string &msg);
  ErrorCode code() const noexcept;
};

} // namespace fc
