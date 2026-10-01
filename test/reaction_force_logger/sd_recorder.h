#ifndef SAVER_REACTION_SD_RECORDER_H
#define SAVER_REACTION_SD_RECORDER_H
#include <Arduino.h>
#include <SdFat.h>
#include "logger_logic.h"

namespace reaction {
// Recording only: do NOT copy this synchronous filesystem layer into a motor ISR.
// FIFO SDIO, preallocation and 512-byte servicing follow SdFat's TeensySdioLogger approach.
class SdRecorder {
public:
  static constexpr size_t CAPACITY = 65536;
  static constexpr uint32_t FILE_BYTES = 32UL * 1024 * 1024;
  bool active() const { return active_; }
  bool busy() const { return active_ || draining_; }
  bool fault() const { return fault_; }
  const char *reason() const { return reason_; }
  const char *filename() const { return name_; }
  size_t queued() const { return queue_.used(); }
  uint32_t rows() const { return rows_; }
  uint32_t maxQueued() const { return highWater_; }
  uint32_t maxWriteUs() const { return maxWriteUs_; }
  uint32_t lostBytes() const { return lostBytes_; }

  bool start(const char *header) {
    if (busy())
      return false;
    queue_.clear();
    rows_ = highWater_ = maxWriteUs_ = lostBytes_ = 0;
    fault_ = false;
    reason_ = "NONE";
    if (!sd_.begin(SdioConfig(FIFO_SDIO)))
      return fail("SD_INIT_FAILED");
    bool found = false;
    for (unsigned i = 0; i < 10000; ++i) {
      snprintf(name_, sizeof(name_), "RF%04u.CSV", i);
      if (sd_.exists(name_))
        continue;
      if (!file_.open(name_, O_RDWR | O_CREAT | O_EXCL))
        return fail("SD_OPEN_FAILED");
      found = true;
      break;
    }
    if (!found)
      return fail("SD_NAME_SPACE_FULL");
    if (!file_.preAllocate(FILE_BYTES))
      return fail("SD_PREALLOC_FAILED_CHECK_SPACE");
    const size_t n = strlen(header);
    if (!queue_.push(header, n))
      return fail("HEADER_TOO_LARGE");
    active_ = true;
    reason_ = "RECORDING";
    return true;
  }
  bool append(const char *line, size_t n) {
    if (!active_)
      return false;
    if (file_.curPosition() + queue_.used() + n + 512 >= FILE_BYTES) {
      stop("FILE_CAPACITY_REACHED");
      return false;
    }
    // Keep space for a termination footer; never store half a CSV row.
    if (queue_.free() < n + 512) {
      stop("BUFFER_OVERFLOW");
      fault_ = true;
      lostBytes_ += n;
      return false;
    }
    queue_.push(line, n);
    ++rows_;
    if (queue_.used() > highWater_)
      highWater_ = queue_.used();
    return true;
  }
  void stop(const char *why) {
    if (!active_)
      return;
    active_ = false;
    draining_ = true;
    drainAt_ = millis();
    reason_ = why;
    char footer[240];
    const int n = snprintf(
        footer, sizeof(footer), "# END,reason=%s,rows=%lu,buffer_high_bytes=%lu,max_write_us=%lu\n",
        why, (unsigned long)rows_, (unsigned long)highWater_, (unsigned long)maxWriteUs_);
    if (n <= 0 || size_t(n) >= sizeof(footer) || !queue_.push(footer, size_t(n)))
      fail("FOOTER_QUEUE_FAILED");
  }
  // At most ONE sector per loop after acquisition. No flush per sample.
  void service() {
    if (!busy())
      return;
    if (draining_ && uint32_t(millis() - drainAt_) >= 3000) {
      fail("SD_DRAIN_TIMEOUT");
      return;
    }
    if (file_.isBusy())
      return;
    const size_t used = queue_.used();
    if (used >= 512 || (draining_ && used)) {
      const size_t n = queue_.peek(sector_, used >= 512 ? 512 : used);
      const uint32_t before = micros();
      const size_t wrote = file_.write(sector_, n);
      const uint32_t elapsed = micros() - before;
      if (elapsed > maxWriteUs_)
        maxWriteUs_ = elapsed;
      queue_.consume(wrote <= n ? wrote : n);
      if (wrote != n || file_.getWriteError())
        fail("SD_WRITE_FAILED");
      return;
    }
    if (draining_) {
      // Filesystem finalization can block in SdFat; timing is NOT hard-real-time certified.
      const bool trimmed = file_.truncate();
      const bool synced = file_.sync();
      const bool closed = file_.close();
      draining_ = false;
      if (!trimmed || !synced || !closed) {
        fault_ = true;
        reason_ = "SD_FINALIZE_FAILED";
      }
    }
  }

private:
  bool fail(const char *why) {
    active_ = draining_ = false;
    fault_ = true;
    reason_ = why;
    lostBytes_ += queue_.used();
    queue_.clear();
    // Do not retry indefinitely or delete evidence after a card/write failure.
    if (file_)
      file_.close();
    return false;
  }
  SdFs sd_;
  FsFile file_;
  ByteQueue<CAPACITY> queue_;
  char sector_[512] = {}, name_[16] = {};
  bool active_ = false, draining_ = false, fault_ = false;
  const char *reason_ = "IDLE";
  uint32_t drainAt_ = 0, rows_ = 0, highWater_ = 0, maxWriteUs_ = 0, lostBytes_ = 0;
};
} // namespace reaction
#endif
