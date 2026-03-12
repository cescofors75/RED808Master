#pragma once
#include <Arduino.h>
#include <LittleFS.h>

// Lightweight file logger for RED808 diagnostics.
// Writes to /log.txt on LittleFS with auto-rotation at 48KB.
// Usage:  syslog("TAG", "message %d", value);

#define SYSLOG_PATH      "/log.txt"
#define SYSLOG_OLD_PATH  "/log_old.txt"
#define SYSLOG_MAX_SIZE  (48 * 1024)  // 48KB max, then rotate

// Set to 0 to disable runtime syslog (keeps BOOT + PANIC only)
// Each syslog call does 2 LittleFS operations + Serial.print → adds latency
#define SYSLOG_RUNTIME_ENABLED 0

void syslogBegin();                        // call once after LittleFS.begin()
void syslog(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void syslogPanic(const char* msg);         // minimal write for crash handler — no alloc
size_t syslogSize();                       // current log file size
void syslogClear();                        // delete log files
