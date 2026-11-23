#include "CodexUsageCore.h"
#include "CodexUsageVersion.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void AssertTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void AssertEqual(int actual, int expected, const char* message) {
    if (actual != expected) {
        std::cerr << message << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

void AssertEqual(const std::wstring& actual, const std::wstring& expected, const char* message) {
    if (actual != expected) {
        std::wcerr << message << L": expected [" << expected << L"], got [" << actual << L"]\n";
        std::exit(1);
    }
}

void AssertNotContains(const std::wstring& actual, wchar_t needle, const char* message) {
    if (actual.find(needle) != std::wstring::npos) {
        std::wcerr << message << L": unexpected character [" << needle << L"] in [" << actual << L"]\n";
        std::exit(1);
    }
}

void ParsesUsageWindows() {
    const std::string json = R"json({
        "email": "user@example.com",
        "plan_type": "pro",
        "rate_limit": {
            "primary_window": {
                "used_percent": 23,
                "limit_window_seconds": 18000,
                "reset_after_seconds": 3600,
                "reset_at": 1780980000
            },
            "secondary_window": {
                "used_percent": 61,
                "limit_window_seconds": 604800,
                "reset_after_seconds": 86400,
                "reset_at": 1781066400
            }
        }
    })json";

    std::wstring error;
    UsageSnapshot snapshot = ParseUsageJson(json, &error);

    AssertTrue(snapshot.success, "snapshot should parse");
    AssertEqual(snapshot.fiveHour.remainingPercent, 77, "five-hour remaining percent");
    AssertEqual(snapshot.weekly.remainingPercent, 39, "weekly remaining percent");
    AssertEqual(snapshot.fiveHour.resetAtUnixSeconds == 1780980000LL ? 1 : 0, 1, "five-hour reset timestamp");
    AssertEqual(snapshot.weekly.resetAtUnixSeconds == 1781066400LL ? 1 : 0, 1, "weekly reset timestamp");
    AssertEqual(snapshot.email, L"user@example.com", "email");
    AssertEqual(snapshot.planType, L"pro", "plan type");
}

void ClassifiesPrimaryWindowWithoutSecondaryWindow() {
    const std::string json = R"json({
        "rate_limit": {
            "primary_window": {
                "used_percent": 4,
                "limit_window_seconds": 604800,
                "reset_after_seconds": 580302,
                "reset_at": 1780980000
            }
        }
    })json";

    std::wstring error;
    UsageSnapshot snapshot = ParseUsageJson(json, &error);

    AssertTrue(snapshot.success, "snapshot should parse without secondary window");
    AssertTrue(error.empty(), "missing secondary window should not set an error");
    AssertEqual(snapshot.fiveHour.remainingPercent, -1, "missing five-hour window should be unavailable");
    AssertEqual(snapshot.weekly.remainingPercent, 96, "primary weekly remaining percent");
    const std::wstring expected =
        L"Codex: 5\u5c0f\u65f6\u5269\u4f59 --\nCodex: \u672c\u5468\u5269\u4f59 96% (\u91cd\u7f6e\u65f6\u95f4 " +
        FormatResetAtLocalTime(1780980000) + L")";
    AssertEqual(BuildUsageTooltip(snapshot), expected,
        "tooltip should show the reset date and time for the available window");
}


void InvalidResetTimestampFallsBackToCountdown() {
    const std::string json = R"json({
        "rate_limit": {
            "primary_window": {
                "used_percent": 23,
                "limit_window_seconds": 18000,
                "reset_after_seconds": 3600,
                "reset_at": -1
            }
        }
    })json";

    std::wstring error;
    const UsageSnapshot snapshot = ParseUsageJson(json, &error);
    AssertTrue(snapshot.success, "invalid reset timestamp should not invalidate usage data");
    AssertEqual(snapshot.fiveHour.resetAtUnixSeconds == 0 ? 1 : 0, 1, "invalid reset timestamp should be ignored");
    AssertTrue(
        BuildUsageTooltip(snapshot).find(L"1h 0m\u540e\u91cd\u7f6e") != std::wstring::npos,
        "invalid reset timestamp should use countdown fallback");
}

void FormatsDisplayText() {
    AssertEqual(FormatRemainingPercent(77), L"77%", "remaining percent text");
    AssertEqual(FormatRemainingPercent(-1), L"--", "negative percent text");
    AssertEqual(FormatRemainingPercent(101), L"--", "out-of-range percent text");
}

void BuildsMultiLineSuccessTooltip() {
    UsageSnapshot snapshot;
    snapshot.success = true;
    snapshot.fiveHour.remainingPercent = 77;
    snapshot.fiveHour.resetAfterSeconds = 3600;
    snapshot.weekly.remainingPercent = 39;
    snapshot.weekly.resetAfterSeconds = 86400;

    const std::wstring tooltip = BuildUsageTooltip(snapshot);

    AssertEqual(
        tooltip,
        L"Codex: 5\u5c0f\u65f6\u5269\u4f59 77% (1h 0m\u540e\u91cd\u7f6e)\nCodex: \u672c\u5468\u5269\u4f59 39% (1d 0h\u540e\u91cd\u7f6e)",
        "multi-line success tooltip");
}


void FormatsResetTimestampAsLocalDateTime() {
    const std::wstring formatted = FormatResetAtLocalTime(1780980000);
    AssertTrue(formatted != L"--", "valid reset timestamp should format");
    AssertEqual(static_cast<int>(formatted.size()), 16, "reset date-time length");
    AssertEqual(formatted.substr(4, 1), L"-", "reset date separator");
    AssertEqual(formatted.substr(7, 1), L"-", "reset date separator");
    AssertEqual(formatted.substr(10, 1), L" ", "reset date-time separator");
    AssertEqual(formatted.substr(13, 1), L":", "reset time separator");
    AssertEqual(FormatResetAtLocalTime(0), L"--", "zero reset timestamp should be unavailable");
    AssertEqual(FormatResetAtLocalTime(-1), L"--", "negative reset timestamp should be unavailable");
}

void BuildsTooltipWithResetDateTime() {
    UsageSnapshot snapshot;
    snapshot.success = true;
    snapshot.fiveHour.remainingPercent = 77;
    snapshot.fiveHour.resetAtUnixSeconds = 1780980000;
    snapshot.weekly.remainingPercent = 39;
    snapshot.weekly.resetAtUnixSeconds = 1781066400;

    const std::wstring expected =
        L"Codex: 5\u5c0f\u65f6\u5269\u4f59 77% (\u91cd\u7f6e\u65f6\u95f4 " +
        FormatResetAtLocalTime(1780980000) +
        L")\nCodex: \u672c\u5468\u5269\u4f59 39% (\u91cd\u7f6e\u65f6\u95f4 " +
        FormatResetAtLocalTime(1781066400) + L")";

    AssertEqual(BuildUsageTooltip(snapshot), expected, "tooltip should show exact local reset date and time");
}

void HasGeneratedVersion() {
    AssertTrue(std::wstring(CODEX_USAGE_VERSION_WIDE).size() > 0, "generated version should not be empty");
}

}  // namespace

void RunCodexConfigTests();

int main() {
    RunCodexConfigTests();
    ParsesUsageWindows();
    ClassifiesPrimaryWindowWithoutSecondaryWindow();
    InvalidResetTimestampFallsBackToCountdown();
    FormatsDisplayText();
    BuildsMultiLineSuccessTooltip();
    FormatsResetTimestampAsLocalDateTime();
    BuildsTooltipWithResetDateTime();
    HasGeneratedVersion();
    return 0;
}
