// String helper functions

#include "strings.h"

#include "constants.h"

std::string ToANSI(const wchar_t* in) {
  if (in == nullptr) {
    return {};
  }
  int len = WideCharToMultiByte(CP_ACP, 0, in, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(len - 1), '\0');
  WideCharToMultiByte(CP_ACP, 0, in, -1, &out[0], len, nullptr, nullptr);
  return out;
}

std::string ToANSI(const std::wstring& in) {
  return ToANSI(in.c_str());
}

std::string ToANSI(const std::wstring* in) {
  return (in != nullptr) ? ToANSI(*in) : std::string{};
}

std::wstring ToWide(const char* in) {
  if (in == nullptr) {
    return {};
  }
  int len = MultiByteToWideChar(CP_ACP, 0, in, -1, nullptr, 0);
  if (len <= 1) {
    return {};
  }
  std::wstring out(static_cast<size_t>(len - 1), L'\0');
  MultiByteToWideChar(CP_ACP, 0, in, -1, &out[0], len);
  return out;
}

std::wstring ToWide(const std::string& in) {
  return ToWide(in.c_str());
}

std::wstring ToWide(const std::string* in) {
  return (in != nullptr) ? ToWide(*in) : std::wstring{};
}

void FormatBytes(wchar_t* buf, size_t cnt, ULONGLONG bytes) {
  const double val = static_cast<double>(bytes);
  if (bytes >= kGB) {
    swprintf(buf, cnt, L"%.2f GB", val / static_cast<double>(kGB));
  } else if (bytes >= kMB) {
    swprintf(buf, cnt, L"%.2f MB", val / static_cast<double>(kMB));
  } else {
    swprintf(buf, cnt, L"%.2f KB", val / static_cast<double>(kKB));
  }
}

void FormatBytesPair(wchar_t* buf, size_t cnt, ULONGLONG used, ULONGLONG limit) {
  const double dused  = static_cast<double>(used);
  const double dlimit = static_cast<double>(limit);
  if (limit >= kGB) {
    swprintf(buf, cnt, L"%.2f / %.2f GB", dused / static_cast<double>(kGB),
             dlimit / static_cast<double>(kGB));
  } else if (limit >= kMB) {
    swprintf(buf, cnt, L"%.2f / %.2f MB", dused / static_cast<double>(kMB),
             dlimit / static_cast<double>(kMB));
  } else {
    swprintf(buf, cnt, L"%.2f / %.2f KB", dused / static_cast<double>(kKB),
             dlimit / static_cast<double>(kKB));
  }
}
