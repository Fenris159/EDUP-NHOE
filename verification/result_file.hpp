#pragma once
#include <windows.h>
#include <string>
inline const wchar_t* verification_result_path = nullptr;
inline bool VerificationResult(const std::string& result) {
  if (!verification_result_path) return false;
  HANDLE file = CreateFileW(verification_result_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD count = 0;
  const bool ok = WriteFile(file, result.data(), static_cast<DWORD>(result.size()), &count, nullptr) && count == result.size();
  CloseHandle(file); return ok;
}
