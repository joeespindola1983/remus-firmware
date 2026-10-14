#include <array>
#include <iostream>

#include "remus/storage/Rms1Codec.hpp"

namespace {
int failures = 0;
void expect(bool value, const char* expression, int line) {
  if (!value) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}
#define EXPECT(value) expect((value), #value, __LINE__)

void testHeaderRecordAndInterruptedTailRecovery() {
  std::array<std::uint8_t, 512> file{};
  remus::storage::Rms1SessionHeader header{0x1122334455667788ULL, 200, 7, 9000,
                                           0xAABBCCDD, 0x10203040, 0, 1, 0};
  EXPECT(remus::storage::encodeSessionHeader(header, file.data(), file.size()).ok());
  EXPECT(file[0] == 'R' && file[3] == '1');

  const std::uint8_t payload[]{1, 2, 3, 4, 5};
  remus::storage::Rms1Record record{remus::storage::Rms1RecordType::ImuBatch,
                                    1, 10000, 2001, 19, 3, payload, sizeof(payload)};
  std::size_t recordBytes = 0;
  EXPECT(remus::storage::encodeRecord(record, file.data() + 64, file.size() - 64,
                                      recordBytes)
             .ok());
  EXPECT(recordBytes == 57);

  remus::storage::Rms1RecoverySummary summary{};
  EXPECT(remus::storage::recoverRms1Prefix(file.data(), 64 + recordBytes, summary).ok());
  EXPECT(summary.recordCount == 1);
  EXPECT(summary.validBytes == 64 + recordBytes);
  EXPECT(!summary.truncatedTail);

  for (std::size_t cut = 64; cut < 64 + recordBytes; ++cut) {
    EXPECT(remus::storage::recoverRms1Prefix(file.data(), cut, summary).ok());
    EXPECT(summary.validBytes == 64);
    EXPECT(summary.truncatedTail == (cut != 64));
  }
}

void testCorruptionIsNotReportedAsValidEvidence() {
  std::array<std::uint8_t, 128> file{};
  remus::storage::Rms1SessionHeader header{1, 2, 3, 4, 5, 6, 0, 1, 0};
  EXPECT(remus::storage::encodeSessionHeader(header, file.data(), file.size()).ok());
  file[16] ^= 0x80;
  remus::storage::Rms1RecoverySummary summary{};
  EXPECT(remus::storage::recoverRms1Prefix(file.data(), 64, summary).error ==
         remus::Error::BadCrc);
}
}  // namespace

int main() {
  testHeaderRecordAndInterruptedTailRecovery();
  testCorruptionIsNotReportedAsValidEvidence();
  return failures == 0 ? 0 : 1;
}
