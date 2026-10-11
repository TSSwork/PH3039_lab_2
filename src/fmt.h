#pragma once
#include <Arduino.h>

// Minimal "{}" formatter for Serial. Each {} prints the next argument
// using Serial.print's own formatting (floats get 2 decimals).
inline void print(const char* f) { Serial.print(f); }

template <typename T, typename... Rest>
void print(const char* f, const T& value, const Rest&... rest) {
  while (*f) {
    if (f[0] == '{' && f[1] == '}') {
      Serial.print(value);
      print(f + 2, rest...);
      return;
    }
    Serial.print(*f++);
  }
}

template <typename... Args>
void println(const char* f, const Args&... args) {
  print(f, args...);
  Serial.println();
}

// CSV data row: goes to console, .log and .csv
template <typename... Args>
void csvln(const char* f, const Args&... args) {
  println(f, args...);
}

// Message: auto-prefixed with "# " so the logger keeps it out of the .csv
template <typename... Args>
void logln(const char* f, const Args&... args) {
  Serial.print("# ");
  println(f, args...);
}
