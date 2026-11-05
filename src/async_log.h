#pragma once
// A log file written on a thread of its own, so a slow disk can't stall whichever thread logs. On the game's
// thread one log line took 4 to 6 s on HadToRegister's drive: long enough for Windows to count the game as not
// responding (it loses focus, the taskbar shows).
//
// The first seconds are written on the spot: a crash during start-up keeps its whole log, and no thread is created
// while Windows may hold its loader lock. After that lines queue and the writer flushes each batch at once, so a
// crash loses only what it hadn't reached. Nothing here ever waits for the writer.
#include <windows.h>
#include <cstdio>
#include <string>

namespace async_log {

class Writer {
public:
    // Called after each flush with the file, under the writer's control (a log's rollover). May replace the file.
    using AfterFlush = void (*)(FILE*& file);
    static constexpr ULONGLONG kOnTheSpotMs = 20000;
    static constexpr size_t kMaxQueued = 8 * 1024 * 1024; // a stalled disk can't grow the game's memory without end

    Writer() { InitializeSRWLock(&lock_); }

    void Open(FILE* file, AfterFlush after = nullptr) {
        file_ = file;
        after_ = after;
        opened_ = GetTickCount64();
    }
    FILE* File() const { return file_; }

    // Text with its line ending. Thread safe.
    void Write(const char* text, size_t length) {
        if (!file_) return;
        AcquireSRWLockExclusive(&lock_);
        if (!background_ && GetTickCount64() - opened_ < kOnTheSpotMs) {
            WriteFile(text, length); // the lock keeps lines whole and in order
            ReleaseSRWLockExclusive(&lock_);
            return;
        }
        if (queued_.size() + length > kMaxQueued) {
            dropped_ += length;
        } else {
            queued_.append(text, length);
        }
        const bool start = !background_;
        background_ = true;
        ReleaseSRWLockExclusive(&lock_);
        if (start) StartThread();
        if (wake_) SetEvent(wake_);
    }

    size_t QueuedForTest() {
        AcquireSRWLockShared(&lock_);
        const size_t n = queued_.size();
        ReleaseSRWLockShared(&lock_);
        return n;
    }

private:
    void WriteFile(const char* text, size_t length) {
#ifdef ASYNC_LOG_TEST
        if (TestDelayMs) Sleep(TestDelayMs);
#endif
        std::fwrite(text, 1, length, file_);
        std::fflush(file_);
        if (after_) after_(file_);
    }
    void StartThread() {
        wake_ = CreateEventW(nullptr, FALSE, TRUE, nullptr);
        // Never ended or joined: a generation's DLL stays loaded after a hot reload, and the thread idles.
        HANDLE thread = wake_ ? CreateThread(nullptr, 0, &Writer::Run, this, 0, nullptr) : nullptr;
        if (thread) {
            CloseHandle(thread);
            return;
        }
        // No thread: fall back to writing on the spot, as before.
        AcquireSRWLockExclusive(&lock_);
        background_ = false;
        opened_ = GetTickCount64();
        if (!queued_.empty()) WriteFile(queued_.data(), queued_.size());
        queued_.clear();
        ReleaseSRWLockExclusive(&lock_);
    }
    static DWORD WINAPI Run(void* self) {
        static_cast<Writer*>(self)->Loop();
        return 0;
    }
    void Loop() {
        std::string batch;
        while (!retired_) { // never set: the thread idles for the life of the game
            WaitForSingleObject(wake_, INFINITE);
            for (;;) {
                size_t dropped = 0;
                AcquireSRWLockExclusive(&lock_);
                batch.swap(queued_);
                dropped = dropped_;
                dropped_ = 0;
                ReleaseSRWLockExclusive(&lock_);
                if (batch.empty() && !dropped) break;
                if (dropped) {
                    char note[96];
                    const int n = std::snprintf(note, sizeof(note), "[log: %zu bytes dropped, the disk was too slow]\n", dropped);
                    if (n > 0) batch.append(note, static_cast<size_t>(n));
                }
                WriteFile(batch.data(), batch.size());
                batch.clear();
            }
        }
    }

    SRWLOCK lock_{};
    FILE* file_ = nullptr;
    AfterFlush after_ = nullptr;
    ULONGLONG opened_ = 0;
    bool background_ = false;
    std::string queued_;
    size_t dropped_ = 0;
    HANDLE wake_ = nullptr;
    volatile bool retired_ = false;

public:
#ifdef ASYNC_LOG_TEST
    DWORD TestDelayMs = 0;
    void TestAgePast() { opened_ = GetTickCount64() - kOnTheSpotMs - 1; }
#endif
};

} // namespace async_log
