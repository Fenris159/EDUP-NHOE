#pragma once

#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace edtrace {
namespace capture_request_detail {
inline constexpr char request[] = "ed-onfoot-event-trace-v1\n";
inline constexpr std::size_t request_size = sizeof(request) - 1;

inline bool Match(HANDLE file, const char* expected) noexcept {
  std::array<char, sizeof(request)> bytes{};
  DWORD count = 0;
  return ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) &&
         count == request_size && std::memcmp(bytes.data(), expected, request_size) == 0;
}
}

struct CaptureRequest {
  std::array<char, capture_request_detail::request_size> contents{};
  BY_HANDLE_FILE_INFORMATION file_info{};
  explicit operator bool() const noexcept { return contents[0] == '!'; }
};

inline CaptureRequest ClaimCaptureRequest(const wchar_t* path) noexcept {
  using namespace capture_request_detail;
  const HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  // Claim the request before opening either recorder. A crash or a failed
  // cleanup leaves a marker that the next process cannot claim.
  CaptureRequest claim{};
  if (Match(file, request)) {
    static std::atomic<std::uint32_t> sequence{0};
    FILETIME started{};
    GetSystemTimeAsFileTime(&started);
    const DWORD process = GetCurrentProcessId();
    const auto attempt = sequence.fetch_add(1, std::memory_order_relaxed);
    claim.contents[0] = '!';
    std::memcpy(claim.contents.data() + 1, &started, sizeof(started));
    std::memcpy(claim.contents.data() + 1 + sizeof(started), &process, sizeof(process));
    std::memcpy(claim.contents.data() + 1 + sizeof(started) + sizeof(process), &attempt, sizeof(attempt));
    DWORD written = 0;
    if (!SetFilePointerEx(file, LARGE_INTEGER{}, nullptr, FILE_BEGIN) ||
        !WriteFile(file, claim.contents.data(), static_cast<DWORD>(claim.contents.size()), &written, nullptr) ||
        written != claim.contents.size() || !FlushFileBuffers(file) ||
        !GetFileInformationByHandle(file, &claim.file_info)) claim = {};
  }
  CloseHandle(file);
  return claim;
}

inline bool CaptureRequestActive(const wchar_t* path, const CaptureRequest& claim) noexcept {
  using namespace capture_request_detail;
  if (!claim) return false;
  constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  const HANDLE file = CreateFileW(path, GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    const bool active = Match(file, claim.contents.data());
    CloseHandle(file);
    return active;
  }
  if (GetLastError() != ERROR_SHARING_VIOLATION) return false;

  // Query metadata when another handle prevents reading the claim contents.
  const HANDLE metadata = CreateFileW(path, 0, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (metadata == INVALID_HANDLE_VALUE) return false;
  BY_HANDLE_FILE_INFORMATION current{};
  const bool observed = GetFileInformationByHandle(metadata, &current) != FALSE;
  CloseHandle(metadata);
  const auto& expected = claim.file_info;
  return observed && current.dwVolumeSerialNumber == expected.dwVolumeSerialNumber &&
         current.nFileIndexHigh == expected.nFileIndexHigh && current.nFileIndexLow == expected.nFileIndexLow &&
         CompareFileTime(&current.ftCreationTime, &expected.ftCreationTime) == 0;
}

inline bool RetireCaptureRequest(const wchar_t* path, const CaptureRequest& claim) noexcept {
  using namespace capture_request_detail;
  const HANDLE file = CreateFileW(path, GENERIC_READ | DELETE, 0, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  // Check and delete the same file instance while replacement is excluded.
  FILE_DISPOSITION_INFO disposition{TRUE};
  const bool removed = claim && Match(file, claim.contents.data()) &&
      SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
  CloseHandle(file);
  return removed;
}
}  // namespace edtrace
