#include "remus/gps/NmeaDecoder.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace remus::gps {
namespace {
int hexDigit(char value) noexcept {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

std::int64_t daysFromCivil(int year, unsigned month, unsigned day) noexcept {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned adjustedMonth = month > 2 ? month - 3U : month + 9U;
  const unsigned doy = (153U * adjustedMonth + 2U) / 5U + day - 1U;
  const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
}  // namespace

void NmeaDecoder::feed(std::uint8_t byte,
                       std::uint64_t receivedAtPcMonotonicUs) noexcept {
  if (byte == '$') {
    collecting_ = true;
    lineSize_ = 0;
    line_[lineSize_++] = '$';
    return;
  }
  if (!collecting_) return;
  if (byte == '\r') return;
  if (byte == '\n') {
    line_[lineSize_] = '\0';
    parseLine(receivedAtPcMonotonicUs);
    collecting_ = false;
    lineSize_ = 0;
    return;
  }
  if (byte < 0x20 || byte > 0x7E || lineSize_ + 1 >= line_.size()) {
    collecting_ = false;
    lineSize_ = 0;
    return;
  }
  line_[lineSize_++] = static_cast<char>(byte);
}

bool NmeaDecoder::parseUnsigned(const char* value, std::uint32_t& out) noexcept {
  if (value == nullptr || *value == '\0') return false;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (end == value || *end != '\0' || parsed > UINT32_MAX) return false;
  out = static_cast<std::uint32_t>(parsed);
  return true;
}

bool NmeaDecoder::parseDecimalScaled(const char* value, std::uint32_t scale,
                                     std::uint32_t& out) noexcept {
  if (value == nullptr || *value == '\0') return false;
  char* end = nullptr;
  const double parsed = std::strtod(value, &end);
  if (end == value || *end != '\0' || !std::isfinite(parsed) || parsed < 0.0) return false;
  const double scaled = std::round(parsed * static_cast<double>(scale));
  if (scaled > static_cast<double>(UINT32_MAX)) return false;
  out = static_cast<std::uint32_t>(scaled);
  return true;
}

bool NmeaDecoder::parseCoordinate(const char* value, const char* hemisphere,
                                  bool latitude, std::int32_t& out) noexcept {
  if (value == nullptr || hemisphere == nullptr || *value == '\0' ||
      hemisphere[0] == '\0' || hemisphere[1] != '\0')
    return false;
  char* end = nullptr;
  const double raw = std::strtod(value, &end);
  if (end == value || *end != '\0' || !std::isfinite(raw)) return false;
  const int degrees = static_cast<int>(raw / 100.0);
  const double minutes = raw - static_cast<double>(degrees * 100);
  double decimal = static_cast<double>(degrees) + minutes / 60.0;
  const char h = hemisphere[0];
  if (h == 'S' || h == 'W') decimal = -decimal;
  else if (h != 'N' && h != 'E') return false;
  const double limit = latitude ? 90.0 : 180.0;
  if (decimal < -limit || decimal > limit) return false;
  out = static_cast<std::int32_t>(std::llround(decimal * 10000000.0));
  return true;
}

bool NmeaDecoder::parseUtc(const char* time, const char* date,
                           std::int64_t& utcMilliseconds) noexcept {
  if (time == nullptr || date == nullptr || std::strlen(time) < 6 || std::strlen(date) != 6)
    return false;
  const auto pair = [](const char* value, int& out) {
    if (value[0] < '0' || value[0] > '9' || value[1] < '0' || value[1] > '9') return false;
    out = (value[0] - '0') * 10 + value[1] - '0';
    return true;
  };
  int hour = 0, minute = 0, second = 0, day = 0, month = 0, shortYear = 0;
  if (!pair(time, hour) || !pair(time + 2, minute) || !pair(time + 4, second) ||
      !pair(date, day) || !pair(date + 2, month) || !pair(date + 4, shortYear) ||
      hour > 23 || minute > 59 || second > 60 || day < 1 || day > 31 || month < 1 ||
      month > 12)
    return false;
  int milliseconds = 0;
  const char* dot = std::strchr(time, '.');
  if (dot != nullptr) {
    int factor = 100;
    for (++dot; *dot >= '0' && *dot <= '9' && factor > 0; ++dot, factor /= 10)
      milliseconds += (*dot - '0') * factor;
  }
  const int year = shortYear >= 80 ? 1900 + shortYear : 2000 + shortYear;
  utcMilliseconds =
      (daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400LL +
       hour * 3600LL + minute * 60LL + second) *
          1000LL +
      milliseconds;
  return true;
}

void NmeaDecoder::parseLine(std::uint64_t receivedAtPcMonotonicUs) noexcept {
  char* star = std::strchr(line_.data(), '*');
  if (star == nullptr || star - line_.data() < 6 || star[1] == '\0' || star[2] == '\0') return;
  std::uint8_t checksum = 0;
  for (char* p = line_.data() + 1; p < star; ++p)
    checksum ^= static_cast<std::uint8_t>(*p);
  const int high = hexDigit(star[1]);
  const int low = hexDigit(star[2]);
  if (high < 0 || low < 0 || checksum != static_cast<std::uint8_t>((high << 4) | low)) {
    ++checksumFailures_;
    return;
  }
  *star = '\0';
  std::array<char*, 20> fields{};
  std::size_t count = 0;
  char* cursor = line_.data() + 1;
  fields[count++] = cursor;
  while (*cursor != '\0' && count < fields.size()) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[count++] = cursor + 1;
    }
    ++cursor;
  }
  if (std::strlen(fields[0]) < 5) return;
  const char* kind = fields[0] + std::strlen(fields[0]) - 3;
  if (std::strcmp(kind, "GGA") == 0 && count >= 9) {
    std::uint32_t quality = 0, satellites = 0, hdop = 0;
    std::int32_t latitude = 0, longitude = 0;
    const bool position = parseCoordinate(fields[2], fields[3], true, latitude) &&
                          parseCoordinate(fields[4], fields[5], false, longitude);
    if (parseUnsigned(fields[6], quality)) snapshot_.fixQuality = static_cast<std::uint8_t>(quality);
    if (parseUnsigned(fields[7], satellites))
      snapshot_.satellites = static_cast<std::uint8_t>(satellites > 254 ? 254 : satellites);
    if (parseDecimalScaled(fields[8], 100, hdop) && hdop <= UINT16_MAX)
      snapshot_.hdopX100 = static_cast<std::uint16_t>(hdop);
    if (position) {
      snapshot_.latitudeE7 = latitude;
      snapshot_.longitudeE7 = longitude;
      snapshot_.positionValid = quality > 0;
    }
    snapshot_.receivedAtPcMonotonicUs = receivedAtPcMonotonicUs;
    ++validSentences_;
    return;
  }
  if (std::strcmp(kind, "RMC") == 0 && count >= 10) {
    const bool active = fields[2][0] == 'A';
    std::int32_t latitude = 0, longitude = 0;
    if (parseCoordinate(fields[3], fields[4], true, latitude) &&
        parseCoordinate(fields[5], fields[6], false, longitude)) {
      snapshot_.latitudeE7 = latitude;
      snapshot_.longitudeE7 = longitude;
      snapshot_.positionValid = active;
    }
    std::uint32_t knotsX1000 = 0;
    if (active && parseDecimalScaled(fields[7], 1000, knotsX1000)) {
      snapshot_.speedMillimetersPerSecond = static_cast<std::uint32_t>(
          std::llround(static_cast<double>(knotsX1000) * 0.514444));
      snapshot_.speedValid = true;
    } else {
      snapshot_.speedValid = false;
    }
    std::uint32_t courseX100 = 0;
    if (active && parseDecimalScaled(fields[8], 100, courseX100) && courseX100 <= 35999) {
      snapshot_.courseCentiDegrees = static_cast<std::uint16_t>(courseX100);
      snapshot_.courseValid = snapshot_.speedValid && snapshot_.speedMillimetersPerSecond > 100;
    } else {
      snapshot_.courseValid = false;
    }
    snapshot_.utcValid = parseUtc(fields[1], fields[9], snapshot_.utcMilliseconds);
    snapshot_.receivedAtPcMonotonicUs = receivedAtPcMonotonicUs;
    ++snapshot_.revision;
    ++validSentences_;
  }
}

}  // namespace remus::gps
