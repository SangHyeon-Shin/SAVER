#include "../compression_current_only_test.ino"
#include <limits.h>

uint32_t mockNow = 0;
std::vector<int> mockOrder;
MockSerial Serial;
MockSerial Serial7(true);
namespace {
unsigned checks = 0, failed = 0;
void check(bool ok, const char *name) {
  ++checks;
  if (!ok) {
    ++failed;
    printf("FAIL: %s\n", name);
  }
}
void boot(CurrentPulse &p, uint32_t at = 0) {
  mockNow = at;
  Serial = MockSerial{};
  Serial7 = MockSerial{true};
  mockOrder.clear();
  p.begin();
  p.tick();
  Serial.output.clear();
  Serial7.frames.clear();
  mockOrder.clear();
}
void command(CurrentPulse &p, const std::string &s) {
  Serial.feed(s + "\n");
  p.tick();
}
int32_t value(const MockFrame &f) {
  const auto &b = f.data;
  const uint32_t u = (uint32_t(b[3]) << 24) | (uint32_t(b[4]) << 16) | (uint32_t(b[5]) << 8) | b[6];
  return u & 0x80000000U ? -1 - int32_t(~u) : int32_t(u);
}
bool lastIs(int32_t wanted) {
  return !Serial7.frames.empty() && value(Serial7.frames.back()) == wanted;
}
bool onlyZero() {
  for (const auto &f : Serial7.frames)
    if (value(f))
      return false;
  return true;
}
bool parsed(const char *s, int32_t wanted) {
  int32_t v = INT32_MAX;
  return parseCurrent(s, v) && v == wanted;
}
bool rejected(const char *s) {
  int32_t v = 123;
  return !parseCurrent(s, v) && v == 123;
}
} // namespace
int main() {
  check(PULSE_MS == 1000 && TX_MS == 100, "fixed one-second pulse, existing 100 ms refresh");
  const uint8_t vector[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  check(crc16(vector, sizeof(vector)) == 0x31C3, "independent CRC-CCITT vector");
  check(parsed("1", 1000) && parsed("-1", -1000), "signed 1 A");
  check(parsed("+0.5", 500) && parsed("-.001", -1), "decimal A to mA");
  check(parsed("15", 15000) && parsed("-15.000", -15000), "previous grammar boundaries accepted");
  check(parsed("16", 16000) && parsed("-16", -16000),
        "old 15 A ceiling removed in both directions");
  check(parsed("15.001", 15001) && parsed("-15.001", -15001), "milliamps above old ceiling");
  // Serialization boundaries below are MOCK/PARSER TEST DATA, never hardware test currents.
  check(parsed("2147483.647", INT32_MAX), "positive signed-int32 mA boundary");
  check(parsed("-2147483.648", INT32_MIN), "negative signed-int32 mA boundary without overflow");
  check(parsed("00016.001", 16001), "leading zeroes preserve exact conversion");
  const char *bad[] = {"",
                       "+",
                       "-",
                       ".",
                       "0",
                       "-0.000",
                       "0.0001",
                       "1.",
                       "NaN",
                       "inf",
                       "1e0",
                       "1A",
                       "1 2",
                       "2147483.648",
                       "-2147483.649",
                       "2147484",
                       "-2147484",
                       "1..0",
                       "99999999999999999999999999"};
  for (const char *s : bad)
    check(rejected(s), s);
  check(rejected(nullptr), "null input");
  {
    CurrentPulse p;
    boot(p);
    p.tick();
    check(onlyZero(), "no automatic drive");
    command(p, "PULSE 1");
    check(lastIs(1000), "positive pulse starts");
    const auto &f = Serial7.frames.back().data;
    check(f.size() == 10 && f[0] == 2 && f[1] == 5 && f[2] == 6 && f[9] == 3,
          "only COMM_SET_CURRENT packet");
    check(f[3] == 0 && f[4] == 0 && f[5] == 3 && f[6] == 0xE8, "1 A is signed int32 1000");
    check(uint16_t((f[7] << 8) | f[8]) == crc16(f.data() + 2, 5), "packet CRC");
    const size_t before = Serial7.frames.size();
    mockNow = TX_MS - 1;
    p.tick();
    check(Serial7.frames.size() == before, "no early refresh");
    mockNow = TX_MS;
    p.tick();
    check(Serial7.frames.size() == before + 1, "configured refresh cadence preserved");
    mockNow = 999;
    p.tick();
    check(lastIs(1000), "pulse active before deadline");
    mockNow = 1000;
    mockOrder.clear();
    p.tick();
    check(lastIs(0), "zero at deadline");
    check(!mockOrder.empty() && mockOrder.front() == 0, "zero queued before event logging");
    const size_t done = Serial7.frames.size();
    mockNow = 2000;
    p.tick();
    check(Serial7.frames.size() == done, "no auto-repeat");
    check(Serial.output.find("PULSE_DONE") != std::string::npos, "completion event");
  }
  {
    CurrentPulse p;
    boot(p);
    command(p, "PULSE -0.5");
    check(lastIs(-500), "negative current preserved");
    mockOrder.clear();
    command(p, "STOP");
    check(lastIs(0) && mockOrder.front() == 0, "manual stop precedes report");
    command(p, "STATUS");
    check(Serial.output.find("command_active=0") != std::string::npos, "stopped status");
    check(Serial.output.find("feedback_verified=0") != std::string::npos,
          "no fake measured status");
  }
  {
    CurrentPulse p;
    boot(p);
    command(p, "PULSE 1");
    command(p, "PULSE -1");
    check(lastIs(0), "second pulse stops instead of instant reversal");
    bool negative = false;
    for (const auto &f : Serial7.frames)
      negative |= value(f) < 0;
    check(!negative, "no reverse packet while active");
  }
  // Mock transport only; representation extremes must NEVER be sent to real hardware.
  const struct {
    const char *command;
    int32_t mA;
  } wireCases[] = {{"PULSE 16", 16000},
                   {"PULSE -16", -16000},
                   {"PULSE 2147483.647", INT32_MAX},
                   {"PULSE -2147483.648", INT32_MIN}};
  for (const auto &item : wireCases) {
    CurrentPulse p;
    boot(p);
    command(p, item.command);
    check(lastIs(item.mA), "widened current is serialized without truncation or sign wrap");
    mockNow = PULSE_MS;
    p.tick();
    check(lastIs(0), "widened command still stops after one second");
  }
  for (const char *s : {"PULSE", "PULSE 1 1000", "PULSE nan", "RUN", "PULSE 0", "PULSE 2147483.648",
                        "PULSE -2147483.649"}) {
    CurrentPulse p;
    boot(p);
    command(p, s);
    check(onlyZero(), s);
  }
  {
    CurrentPulse p;
    boot(p);
    command(p, "PULSE 1");
    command(p, "BAD");
    check(lastIs(0), "invalid command cancels pulse");
  }
  {
    CurrentPulse p;
    boot(p);
    command(p, "PULSE 1");
    Serial.connected = false;
    p.tick();
    check(lastIs(0), "USB disconnect requests zero");
    Serial.connected = true;
    Serial.feed("PULSE 1\n");
    p.tick();
    check(lastIs(0), "reconnect discards stale commands");
    mockNow = TX_MS;
    p.tick();
    check(lastIs(0), "no resume after reconnect");
  }
  {
    CurrentPulse p;
    boot(p);
    command(p, "PULSE 1");
    Serial7.space = 0;
    mockNow = TX_MS;
    p.tick();
    Serial.output.clear();
    command(p, "STATUS");
    check(Serial.output.find("command_active=0") != std::string::npos, "TX full cancels drive");
    check(Serial.output.find("stop_pending=1") != std::string::npos,
          "TX full exposes pending zero");
    Serial7.space = 4096;
    p.tick();
    check(lastIs(0), "retry zero before any drive");
    const size_t done = Serial7.frames.size();
    mockNow = TX_MS * 2;
    p.tick();
    check(Serial7.frames.size() == done, "TX recovery does not resume");
  }
  {
    CurrentPulse p;
    boot(p);
    Serial7.space = 0;
    command(p, "PULSE 1");
    check(onlyZero(), "no accepted pulse with blocked initial TX");
    Serial7.space = 4096;
    p.tick();
    check(lastIs(0), "failed start only retries zero");
  }
  {
    CurrentPulse p;
    boot(p);
    command(p, "PULSE 1");
    command(p, std::string(60, 'X'));
    check(lastIs(0), "oversized line stops pulse");
    command(p, "PULSE 0.5\r");
    check(lastIs(500), "line parser recovers after newline and CRLF");
  }
  {
    CurrentPulse p;
    boot(p);
    Serial.feed("PULSE 1");
    p.tick();
    mockNow = 1000;
    p.tick();
    check(onlyZero(), "partial-line timeout cannot start pulse");
    Serial.feed("\n");
    p.tick();
    check(onlyZero(), "expired command is discarded");
  }
  {
    CurrentPulse p;
    boot(p, UINT32_MAX - 500);
    command(p, "PULSE 1");
    mockNow += 999;
    p.tick();
    check(lastIs(1000), "pulse before timer wrap deadline");
    ++mockNow;
    p.tick();
    check(lastIs(0), "one-second stop across timer wrap");
  }
  {
    CurrentPulse p;
    boot(p);
    Serial.space = 0;
    command(p, "PULSE 1");
    mockNow = 1000;
    p.tick();
    check(lastIs(0), "USB backpressure cannot delay timeout");
  }
  {
    CurrentPulse p;
    boot(p);
    command(p, "RUN 1");
    check(lastIs(1000) && Serial.output.find("OK,RUN_STARTED") != std::string::npos,
          "explicit RUN starts manual current only");
    for (uint32_t at : {999U, 1000U, 1001U, 30000U, 600000U, 3600000U}) {
      mockNow = at;
      p.tick();
      check(lastIs(1000), "manual RUN has no hidden one-second or long-run deadline");
    }
    Serial.output.clear();
    command(p, "STATUS");
    check(Serial.output.find("command_active=1") != std::string::npos &&
              Serial.output.find("run_mode=MANUAL_STOP") != std::string::npos &&
              Serial.output.find("remaining_ms=") == std::string::npos,
          "manual status does not invent a remaining time");
    mockOrder.clear();
    command(p, "STOP");
    check(lastIs(0) && !mockOrder.empty() && mockOrder.front() == 0,
          "manual STOP queues zero before report");
    const size_t stopped = Serial7.frames.size();
    mockNow += 1000;
    p.tick();
    check(Serial7.frames.size() == stopped, "manual STOP never auto-resumes");
    Serial.output.clear();
    command(p, "STATUS");
    check(Serial.output.find("run_mode=STOPPED") != std::string::npos,
          "stopped manual mode is explicit");
    command(p, "PULSE 1");
    Serial.output.clear();
    command(p, "STATUS");
    check(Serial.output.find("run_mode=PULSE_1S") != std::string::npos &&
              Serial.output.find("remaining_ms=1000") != std::string::npos,
          "PULSE still exposes a real one-second countdown");
    mockNow += 1000;
    p.tick();
    check(lastIs(0), "PULSE after manual RUN still expires");
  }
  for (const char *start : {"RUN -0.5", "RUN 1"}) {
    CurrentPulse p;
    boot(p);
    command(p, start);
    check(lastIs(start[4] == '-' ? -500 : 1000), "manual signed current is preserved");
    Serial.connected = false;
    p.tick();
    check(lastIs(0), "manual RUN stops on USB disconnect");
    Serial.connected = true;
    Serial.feed("RUN 1\n");
    p.tick();
    mockNow += 1000;
    p.tick();
    check(lastIs(0), "manual RUN cannot survive or replay across USB reconnect");
  }
  for (const char *second : {"RUN -1", "PULSE -1", "RUN 1"}) {
    CurrentPulse p;
    boot(p);
    command(p, "RUN 1");
    command(p, second);
    check(lastIs(0), "active manual command stops rather than changing or reversing current");
    bool negative = false;
    for (const auto &f : Serial7.frames)
      negative |= value(f) < 0;
    check(!negative, "no immediate reverse packet in manual mode");
  }
  for (const char *badRun : {"RUN", "RUN 0", "RUN 1 1000", "RUN nan", "RUN 1A", "run 1",
                             "RUN 2147483.648", "RUN -2147483.649"}) {
    CurrentPulse p;
    boot(p);
    command(p, "RUN 1");
    command(p, badRun);
    check(lastIs(0), "bad manual input cancels current");
  }
  {
    CurrentPulse p;
    boot(p);
    command(p, "RUN 1");
    Serial7.space = 0;
    mockNow += TX_MS;
    p.tick();
    Serial.output.clear();
    command(p, "STATUS");
    check(Serial.output.find("command_active=0") != std::string::npos &&
              Serial.output.find("stop_pending=1") != std::string::npos,
          "manual UART congestion stops and exposes pending zero");
    Serial7.space = 4096;
    p.tick();
    check(lastIs(0), "manual UART recovery sends zero only");
    const size_t stopped = Serial7.frames.size();
    mockNow += 1000;
    p.tick();
    check(Serial7.frames.size() == stopped, "manual UART recovery never resumes");
  }
  {
    CurrentPulse p;
    boot(p);
    Serial7.space = 0;
    command(p, "RUN 1");
    check(onlyZero(), "manual RUN cannot start while initial UART is blocked");
    Serial7.space = 4096;
    p.tick();
    check(lastIs(0), "failed manual start retries zero without replaying RUN");
  }
  {
    CurrentPulse p;
    boot(p, UINT32_MAX - 500);
    command(p, "RUN 1");
    mockNow += 1000;
    p.tick();
    check(lastIs(1000), "manual RUN survives millis rollover without auto-expiring");
    Serial.space = 0;
    command(p, "STOP");
    check(lastIs(0), "manual STOP is not blocked by USB log congestion");
  }
  for (bool malformed : {false, true}) {
    CurrentPulse p;
    boot(p);
    command(p, "RUN 1");
    if (malformed) {
      command(p, std::string(60, 'X'));
    } else {
      Serial.feed("RUN 1");
      p.tick();
      mockNow += 1000;
      p.tick();
    }
    check(lastIs(0), "manual RUN stops on line overflow or incomplete command timeout");
  }
  printf("%u checks, %u failures (mock USB/UART/time; no physical hardware).\n", checks, failed);
  return failed ? 1 : 0;
}
