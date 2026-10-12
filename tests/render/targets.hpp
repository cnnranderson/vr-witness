#pragma once
#include <atomic>
extern std::atomic<unsigned> submissions, cursors, before_cursor, after_cursor, outside;
extern int stage;
extern "C" __declspec(dllexport) void TestSubmit(int eye);
extern "C" __declspec(dllexport) bool TestBeginEye(int eye);
extern "C" __declspec(dllexport) void TestCursor(void*, void*, bool);
extern "C" __declspec(dllexport) void TestMenu(bool stereo, int eye, bool mirror);
extern std::atomic<unsigned> menu_calls;
extern std::atomic<unsigned> after_menu;
extern std::atomic<bool> fixture_paused;
extern "C" __declspec(dllexport) bool TestPaused();
extern std::atomic<unsigned> hands_calls, hands_bad_stage, hands_bad_eye;
extern bool fixture_hands_result, fixture_cursor_inside_begin;
extern "C" __declspec(dllexport) bool TestDrawHands(int eye);
int check_hands_hook();
