#include "remus/diagnostics/RotatingTextLog.hpp"

#include <cerrno>
#include <cstring>

#include <unistd.h>

namespace remus::diagnostics {

namespace {

Status ioFailure() noexcept {
  return Status::failure(Error::IoFailure, static_cast<std::uint32_t>(errno));
}

}  // namespace

RotatingTextLog::RotatingTextLog(RotatingTextLogConfig config) noexcept : config_(config) {}

RotatingTextLog::~RotatingTextLog() { close(); }

Status RotatingTextLog::open() noexcept {
  if (file_ != nullptr) {
    return Status::success();
  }
  if (config_.activePath == nullptr || config_.archivePath == nullptr ||
      config_.segmentBytes == 0 || config_.maximumLineBytes == 0 ||
      config_.maximumLineBytes + 1 > config_.segmentBytes) {
    return Status::failure(Error::InvalidArgument);
  }

  file_ = std::fopen(config_.activePath, "ab+");
  if (file_ == nullptr) {
    return ioFailure();
  }
  if (std::fseek(file_, 0, SEEK_END) != 0) {
    const Status status = ioFailure();
    close();
    return status;
  }
  const long position = std::ftell(file_);
  if (position < 0) {
    const Status status = ioFailure();
    close();
    return status;
  }
  stats_.activeBytes = static_cast<std::uint64_t>(position);
  // Reserve enough room for the largest accepted line before asynchronous
  // writers start. This keeps a nearly-full file from forcing its first
  // rotation in the writer task while another startup operation opens a
  // second file on the same FAT volume.
  if (stats_.activeBytes + config_.maximumLineBytes + 1 > config_.segmentBytes) {
    return rotate();
  }
  return Status::success();
}

Status RotatingTextLog::appendLine(const char* line) noexcept {
  if (file_ == nullptr) {
    return Status::failure(Error::NotReady);
  }
  if (line == nullptr) {
    return Status::failure(Error::InvalidArgument);
  }

  const std::size_t length = strnlen(line, config_.maximumLineBytes + 1);
  if (length > config_.maximumLineBytes) {
    return Status::failure(Error::BufferOverrun,
                           static_cast<std::uint32_t>(config_.maximumLineBytes));
  }
  const std::size_t recordBytes = length + 1;
  if (stats_.activeBytes + recordBytes > config_.segmentBytes) {
    const Status rotateStatus = rotate();
    if (!rotateStatus.ok()) {
      ++stats_.appendFailures;
      return rotateStatus;
    }
  }

  if (std::fwrite(line, 1, length, file_) != length || std::fputc('\n', file_) == EOF) {
    ++stats_.appendFailures;
    return ioFailure();
  }
  stats_.activeBytes += recordBytes;
  return sync();
}

Status RotatingTextLog::close() noexcept {
  if (file_ == nullptr) {
    return Status::success();
  }
  const Status syncStatus = sync();
  const int closeResult = std::fclose(file_);
  file_ = nullptr;
  if (!syncStatus.ok()) {
    return syncStatus;
  }
  return closeResult == 0 ? Status::success() : ioFailure();
}

Status RotatingTextLog::rotate() noexcept {
  if (file_ != nullptr) {
    if (std::fclose(file_) != 0) {
      file_ = nullptr;
      return ioFailure();
    }
    file_ = nullptr;
  }

  errno = 0;
  if (std::remove(config_.archivePath) != 0 && errno != ENOENT) {
    return ioFailure();
  }
  if (std::rename(config_.activePath, config_.archivePath) != 0 && errno != ENOENT) {
    return ioFailure();
  }

  file_ = std::fopen(config_.activePath, "wb");
  if (file_ == nullptr) {
    return ioFailure();
  }
  stats_.activeBytes = 0;
  ++stats_.rotations;
  return Status::success();
}

Status RotatingTextLog::sync() noexcept {
  if (file_ == nullptr) {
    return Status::failure(Error::NotReady);
  }
  if (std::fflush(file_) != 0 || ::fsync(fileno(file_)) != 0) {
    ++stats_.syncFailures;
    return ioFailure();
  }
  return Status::success();
}

}  // namespace remus::diagnostics
