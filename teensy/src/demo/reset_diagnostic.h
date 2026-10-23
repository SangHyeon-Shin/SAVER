#ifndef SAVER_DEMO_RESET_DIAGNOSTIC_H
#define SAVER_DEMO_RESET_DIAGNOSTIC_H
#include "bench_io.h"
#if defined(__IMXRT1062__)
#include <CrashReport.h>
#endif
namespace saver_demo {
// Capture only while boot outputs are OFF. No Serial calls or waits: the saved
// core report is replayed as bounded JSON log lines after USB connects.
class ResetDiagnostic {
public:
  void capture() {
#if defined(__IMXRT1062__)
    resetRegister_ = SRC_SRSR;
    registerAvailable_ = true;
    crashAvailable_ = bool(CrashReport);
    if (crashAvailable_) {
      class Sink : public Print {
      public:
        explicit Sink(ResetDiagnostic &target) : target_(target) {}
        size_t write(uint8_t c) override {
          target_.append(char(c));
          return 1;
        }

      private:
        ResetDiagnostic &target_;
      } sink(*this);
      CrashReport.printTo(sink);
    }
#endif
  }
  void report(selectable::Log &log) const {
    log.emit("BOOT_DIAG,reset_register_available=%u,crash_record=%u,truncated=%u",
             registerAvailable_, crashAvailable_, truncated_);
    if (registerAvailable_)
      log.emit("BOOT_RESET_REGISTER,SRC_SRSR=0x%08lX", (unsigned long)resetRegister_);
    char line[161] = {};
    size_t used = 0;
    unsigned lines = 0;
    for (size_t i = 0; i < size_ && lines < 24; ++i) {
      if (text_[i] != '\r' && text_[i] != '\n')
        line[used++] = text_[i];
      if (text_[i] == '\n' || used == sizeof(line) - 1 || i + 1 == size_) {
        if (used) {
          line[used] = 0;
          log.emit("CRASH_REPORT,%s", line);
          ++lines;
          used = 0;
        }
      }
    }
    if (lines == 24)
      log.emit("BOOT_DIAG_REPORT_LINE_LIMIT");
  }

private:
  void append(char c) {
    if (size_ < sizeof(text_))
      text_[size_++] = c;
    else
      truncated_ = true;
  }
  char text_[2048] = {};
  size_t size_ = 0;
  uint32_t resetRegister_ = 0;
  bool registerAvailable_ = false, crashAvailable_ = false, truncated_ = false;
};
} // namespace saver_demo
#endif
