#include "FileOps.h"

#include "platform/win/WinError.h"

#include <windows.h>

#include <winioctl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace icd::testvol {

    namespace {

        // Throws with the Win32 error text for a failed call on a path.
        [[noreturn]] void fail(std::wstring_view what, const std::wstring& path) {
            const std::wstring text = std::wstring(what) + L" " + path + L": " + platform::lastWinError();
            throw std::runtime_error(std::string(text.begin(), text.end()));
        }

        // Owns a Win32 file handle.
        class Handle {
        public:
            explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : h_(h) {}
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            Handle(Handle&& other) noexcept : h_(std::exchange(other.h_, INVALID_HANDLE_VALUE)) {}
            Handle& operator=(Handle&& other) noexcept {
                std::swap(h_, other.h_);
                return *this;
            }
            ~Handle() {
                if (h_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(h_);
                }
            }
            // The raw handle for API calls.
            HANDLE get() const { return h_; }
            // True when the open succeeded.
            bool valid() const { return h_ != INVALID_HANDLE_VALUE; }

        private:
            HANDLE h_; // Owned handle or INVALID_HANDLE_VALUE.
        };

        // Opens a path with the given access and disposition; throws on failure.
        Handle openFile(const std::wstring& path, DWORD access, DWORD disposition, DWORD flags = 0) {
            Handle h(CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 disposition, FILE_ATTRIBUTE_NORMAL | flags, nullptr));
            if (!h.valid()) {
                fail(L"cannot open", path);
            }
            return h;
        }

        // Words for text content: few and repeated, so it compresses well.
        constexpr std::array<std::string_view, 16> kWords{
            "cluster ", "extent ", "volume ", "record ", "stripe ", "bitmap ", "journal ", "sector ",
            "\n",       "return ", "struct ", "const ",  "{ ",      "} ",      "int ",     "auto "};

        // Deterministic byte stream of one file: the same seed always produces the same bytes.
        class ContentStream {
        public:
            ContentStream(std::uint64_t seed, Content kind) : rng_(seed), kind_(kind) {}

            // Fills the buffer with the next bytes of the stream.
            void fill(std::byte* out, std::size_t count) {
                if (kind_ == Content::Text) {
                    fillText(out, count);
                }
                else {
                    fillRandom(out, count);
                }
            }

        private:
            void fillRandom(std::byte* out, std::size_t count) {
                std::size_t i = 0;
                for (; i + 8 <= count; i += 8) {
                    const std::uint64_t v = rng_.next();
                    std::memcpy(out + i, &v, 8);
                }
                const std::uint64_t v = rng_.next();
                std::memcpy(out + i, &v, count - i);
            }

            void fillText(std::byte* out, std::size_t count) {
                for (std::size_t i = 0; i < count; ++i) {
                    if (pos_ == word_.size()) {
                        word_ = kWords[rng_.below(kWords.size())];
                        pos_ = 0;
                    }
                    out[i] = static_cast<std::byte>(word_[pos_++]);
                }
            }

            core::SynthRandom rng_; // Byte source.
            Content kind_; // Random or text.
            std::string_view word_; // Current word of text content.
            std::size_t pos_ = 0; // Next character of word_.
        };

        // A file being written by an interleaved batch.
        struct OpenFile {
            FileSpec spec; // What to write.
            Handle handle; // Open for writing.
            ContentStream content; // Its bytes.
            std::uint64_t written = 0; // Bytes written so far.
        };

        // Writes bytes from a content stream to a handle in buffer-sized pieces.
        void writeBytes(HANDLE h, ContentStream& content, std::uint64_t bytes, std::vector<std::byte>& buffer,
                        const std::wstring& path) {
            while (bytes > 0) {
                const auto piece = static_cast<DWORD>(std::min<std::uint64_t>(bytes, buffer.size()));
                content.fill(buffer.data(), piece);
                DWORD done = 0;
                if (WriteFile(h, buffer.data(), piece, &done, nullptr) == FALSE || done != piece) {
                    fail(L"cannot write", path);
                }
                bytes -= piece;
            }
        }

        // Parent directory of a relative path ("" for files in the root).
        std::wstring parentOf(const std::wstring& rel) {
            const auto slash = rel.find_last_of(L'\\');
            return slash == std::wstring::npos ? std::wstring() : rel.substr(0, slash);
        }

        // Joins one file's retrieval pointers into on-disk pieces; returns the piece count.
        std::uint64_t piecesOf(HANDLE h) {
            std::vector<std::byte> out(64 * 1024);
            STARTING_VCN_INPUT_BUFFER in{};
            std::uint64_t pieces = 0;
            LONGLONG nextLcn = -1;
            for (;;) {
                DWORD bytes = 0;
                const BOOL ok = DeviceIoControl(h, FSCTL_GET_RETRIEVAL_POINTERS, &in, sizeof(in), out.data(),
                                                static_cast<DWORD>(out.size()), &bytes, nullptr);
                const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
                if (!ok && err != ERROR_MORE_DATA) {
                    return pieces; // Resident or empty (ERROR_HANDLE_EOF), or not readable.
                }
                const auto* rp = reinterpret_cast<const RETRIEVAL_POINTERS_BUFFER*>(out.data());
                LONGLONG vcn = rp->StartingVcn.QuadPart;
                for (DWORD i = 0; i < rp->ExtentCount; ++i) {
                    const LONGLONG lcn = rp->Extents[i].Lcn.QuadPart;
                    const LONGLONG length = rp->Extents[i].NextVcn.QuadPart - vcn;
                    if (lcn != -1) {
                        pieces += lcn == nextLcn ? 0 : 1;
                        nextLcn = lcn + length;
                    }
                    vcn = rp->Extents[i].NextVcn.QuadPart;
                }
                if (ok) {
                    return pieces;
                }
                in.StartingVcn.QuadPart = vcn;
            }
        }

        // Adds one file's pieces to the totals.
        void countFile(const std::wstring& path, FragmentStats& stats) {
            Handle h(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (!h.valid()) {
                return;
            }
            const std::uint64_t pieces = piecesOf(h.get());
            if (pieces == 0) {
                return;
            }
            ++stats.files;
            stats.fragments += pieces;
            stats.fragmented += pieces > 1 ? 1 : 0;
            stats.maxFragments = std::max(stats.maxFragments, pieces);
        }

        // Recursive walk for countFragments(); dir is a full path ending without a backslash.
        void countTree(const std::wstring& dir, FragmentStats& stats) {
            WIN32_FIND_DATAW data{};
            const std::wstring pattern = dir + L"\\*";
            HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                           FIND_FIRST_EX_LARGE_FETCH);
            if (find == INVALID_HANDLE_VALUE) {
                return;
            }
            do {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L".." || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    continue;
                }
                const std::wstring path = dir + L"\\" + name;
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    countTree(path, stats);
                }
                else {
                    countFile(path, stats);
                }
            }
            while (FindNextFileW(find, &data) != FALSE);
            FindClose(find);
        }

    } // namespace

    Generator::Generator(std::wstring root, std::uint64_t seed, std::uint64_t budget) :
        root_(std::move(root)), rng_(seed), budget_(budget), seedCounter_(seed << 32U) {}

    std::wstring Generator::full(const std::wstring& rel) const { return L"\\\\?\\" + root_ + rel; }

    void Generator::makeDirs(const std::wstring& relDir) {
        if (relDir.empty() || dirs_.contains(relDir)) {
            return;
        }
        makeDirs(parentOf(relDir));
        const std::wstring path = full(relDir);
        if (CreateDirectoryW(path.c_str(), nullptr) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS) {
            fail(L"cannot create directory", path);
        }
        dirs_.insert(relDir);
    }

    std::uint64_t Generator::usedBytes() const {
        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER total{};
        if (GetDiskFreeSpaceExW(root_.c_str(), nullptr, &total, &freeBytes) == FALSE) {
            fail(L"cannot read free space of", root_);
        }
        return total.QuadPart - freeBytes.QuadPart;
    }

    std::uint64_t Generator::take(std::uint64_t want) {
        const std::uint64_t granted = std::min(want, budget_);
        budget_ -= granted;
        return granted;
    }

    std::vector<FileSpec> Generator::write(std::vector<FileSpec> specs, WriteMode mode) {
        std::vector<FileSpec> written;
        std::vector<OpenFile> active;
        std::vector<std::byte> buffer(static_cast<std::size_t>(mode.chunk));
        std::size_t next = 0;
        const auto refill = [&] {
            while (static_cast<int>(active.size()) < mode.streams && next < specs.size()) {
                FileSpec spec = specs[next++];
                const std::uint64_t wanted = spec.size;
                spec.size = take(wanted);
                if (wanted > 0 && spec.size == 0) {
                    continue; // Out of budget: this file no longer fits.
                }
                makeDirs(parentOf(spec.path));
                Handle h = openFile(full(spec.path), GENERIC_WRITE, CREATE_ALWAYS);
                ++totals_.filesWritten;
                active.push_back(OpenFile{spec, std::move(h), ContentStream(spec.seed, spec.content)});
            }
        };
        refill();
        while (!active.empty()) {
            const std::size_t i = rng_.below(active.size());
            OpenFile& file = active[i];
            const std::uint64_t piece = std::min(mode.chunk, file.spec.size - file.written);
            writeBytes(file.handle.get(), file.content, piece, buffer, file.spec.path);
            file.written += piece;
            totals_.bytesWritten += piece;
            if (file.written == file.spec.size) {
                written.push_back(file.spec);
                std::swap(active[i], active.back());
                active.pop_back();
                refill();
            }
        }
        return written;
    }

    void Generator::append(const std::wstring& rel, std::uint64_t bytes) {
        bytes = take(bytes);
        if (bytes == 0) {
            return;
        }
        Handle h = openFile(full(rel), FILE_APPEND_DATA, OPEN_EXISTING);
        std::vector<std::byte> buffer(static_cast<std::size_t>(std::min<std::uint64_t>(bytes, 1ULL << 20U)));
        ContentStream content(nextSeed(), Content::Random);
        writeBytes(h.get(), content, bytes, buffer, rel);
        totals_.bytesWritten += bytes;
    }

    void Generator::remove(const FileSpec& file) {
        const std::wstring path = full(file.path);
        if (DeleteFileW(path.c_str()) == FALSE) {
            fail(L"cannot delete", path);
        }
        budget_ += file.size;
        ++totals_.filesDeleted;
    }

    std::vector<FileSpec> Generator::removeSome(std::vector<FileSpec> files, double share) {
        std::vector<FileSpec> kept;
        for (const FileSpec& file : files) {
            if (rng_.unit() < share) {
                remove(file);
            }
            else {
                kept.push_back(file);
            }
        }
        return kept;
    }

    std::uint64_t logNormalSize(core::SynthRandom& rng, double median, double sigma, std::uint64_t lo,
                                std::uint64_t hi) {
        const double v = median * std::exp(sigma * rng.normal());
        return std::clamp(static_cast<std::uint64_t>(std::max(v, 0.0)), lo, std::max(lo, hi));
    }

    void setCompressed(const Generator& gen, const std::wstring& relDir) {
        const std::wstring path = gen.full(relDir);
        Handle h = openFile(path, GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
        USHORT format = COMPRESSION_FORMAT_DEFAULT;
        DWORD bytes = 0;
        if (DeviceIoControl(h.get(), FSCTL_SET_COMPRESSION, &format, sizeof(format), nullptr, 0, &bytes, nullptr) ==
            FALSE) {
            fail(L"cannot compress", path);
        }
    }

    void writeSparse(Generator& gen, const std::wstring& rel, std::uint64_t size, int blocks,
                     std::uint64_t blockBytes) {
        const std::wstring path = gen.full(rel);
        Handle h = openFile(path, GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS);
        DWORD bytes = 0;
        if (DeviceIoControl(h.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes, nullptr) == FALSE) {
            fail(L"cannot make sparse", path);
        }
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>(size);
        if (SetFilePointerEx(h.get(), end, nullptr, FILE_BEGIN) == FALSE || SetEndOfFile(h.get()) == FALSE) {
            fail(L"cannot size", path);
        }
        std::vector<std::byte> buffer(static_cast<std::size_t>(blockBytes));
        ContentStream content(gen.nextSeed(), Content::Random);
        const std::uint64_t slots = size / blockBytes;
        for (int i = 0; i < blocks; ++i) {
            LARGE_INTEGER at{};
            at.QuadPart = static_cast<LONGLONG>(gen.rng().below(slots) * blockBytes);
            if (SetFilePointerEx(h.get(), at, nullptr, FILE_BEGIN) == FALSE) {
                fail(L"cannot seek", path);
            }
            writeBytes(h.get(), content, blockBytes, buffer, rel);
        }
    }

    void hardLink(const Generator& gen, const std::wstring& link, const std::wstring& target) {
        if (CreateHardLinkW(gen.full(link).c_str(), gen.full(target).c_str(), nullptr) == FALSE) {
            fail(L"cannot hard link", link);
        }
    }

    void symbolicLink(const Generator& gen, const std::wstring& link, const std::wstring& target, bool directory) {
        const DWORD flags =
            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE | (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
        const std::wstring stored = gen.root() + target;
        if (CreateSymbolicLinkW(gen.full(link).c_str(), stored.c_str(), flags) == FALSE) {
            fail(L"cannot create symbolic link", link);
        }
    }

    void junction(const Generator& gen, const std::wstring& link, const std::wstring& target) {
        const std::wstring cmd =
            L"cmd.exe /c mklink /J \"" + gen.root() + link + L"\" \"" + gen.root() + target + L"\" >nul";
        if (runProcess(cmd) != 0) {
            throw std::runtime_error("mklink /J failed");
        }
    }

    void encrypt(const Generator& gen, const std::wstring& rel) {
        const std::wstring path = gen.root() + rel;
        if (EncryptFileW(path.c_str()) == FALSE) {
            fail(L"cannot encrypt", path);
        }
    }

    void setAttributes(const Generator& gen, const std::wstring& rel, unsigned long attributes) {
        if (SetFileAttributesW(gen.full(rel).c_str(), attributes) == FALSE) {
            fail(L"cannot set attributes", rel);
        }
    }

    FragmentStats countFragments(const Generator& gen, const std::wstring& rel) {
        FragmentStats stats;
        std::wstring dir = gen.full(rel);
        while (!dir.empty() && dir.back() == L'\\') {
            dir.pop_back();
        }
        countTree(dir, stats);
        return stats;
    }

    unsigned long runProcess(const std::wstring& commandLine) {
        std::wstring mutableLine = commandLine;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) ==
            FALSE) {
            fail(L"cannot start", commandLine);
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return code;
    }

} // namespace icd::testvol
