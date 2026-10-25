// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "output_pipeline.h"
#include "text_logic.h"
#include "zlib.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace workbench
{
namespace
{
namespace fs = std::filesystem;
constexpr std::size_t chunkSize = 65536;
#ifdef _WIN32
[[noreturn]] void windowsError(DWORD error, const char *operation)
{
    throw std::system_error(static_cast<int>(error), std::system_category(),
        std::string(operation) + " [Win32 " + std::to_string(error) + "]");
}
fs::path currentDirectory()
{
    std::vector<wchar_t> buffer(256);
    for (;;)
    {
        const auto count = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (!count) { windowsError(GetLastError(), "Cannot get export working directory"); }
        if (count < buffer.size()) { return fs::path(std::wstring(buffer.data(), count)); }
        buffer.resize(static_cast<std::size_t>(count) + 1);
    }
}
fs::path absolutePath(const fs::path &path)
{
    const auto name = path.native();
    if (name.rfind(L"\\\\?\\", 0) == 0 || name.rfind(L"\\\\.\\", 0) == 0) { return path; }
    std::vector<wchar_t> buffer(256);
    for (;;)
    {
        const auto count = GetFullPathNameW(name.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (!count) { windowsError(GetLastError(), "Cannot resolve export path"); }
        if (count < buffer.size())
        {
            const std::wstring full(buffer.data(), count);
            if (full.rfind(L"\\\\", 0) == 0) { return fs::path(L"\\\\?\\UNC\\" + full.substr(2)); }
            return fs::path(L"\\\\?\\" + full);
        }
        buffer.resize(static_cast<std::size_t>(count) + 1);
    }
}
void removeTemporary(const fs::path &path) noexcept
{
    DeleteFileW(path.c_str());
}
#else
fs::path currentDirectory() { return fs::current_path(); }
fs::path absolutePath(const fs::path &path) { return fs::absolute(path).lexically_normal(); }
void removeTemporary(const fs::path &path) noexcept
{
    std::error_code ignored;
    fs::remove(path, ignored);
}
#endif
void checkpoint(const OutputControl &control)
{
    if (control.checkpoint) { control.checkpoint(); }
}
void report(const OutputControl &control, int value)
{
    if (control.progress) { control.progress(value); }
}
[[noreturn]] void fileError(const char *operation)
{
    throw std::system_error(errno, std::generic_category(), operation);
}
#ifdef _WIN32
class File
{
  public:
    explicit File(HANDLE handle) : handle_(handle)
    {
        if (handle_ == INVALID_HANDLE_VALUE) { windowsError(GetLastError(), "Cannot open export file"); }
    }
    explicit File(const fs::path &path)
        : File(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) {}
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    ~File() { if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); } }
    std::size_t read(void *data, std::size_t capacity)
    {
        DWORD count = 0;
        const auto requested = static_cast<DWORD>(std::min<std::size_t>(capacity, MAXDWORD));
        if (!ReadFile(handle_, data, requested, &count, nullptr))
        {
            windowsError(GetLastError(), "Cannot read export file");
        }
        return count;
    }
    void write(const void *data, std::size_t count)
    {
        auto *bytes = static_cast<const unsigned char *>(data);
        while (count)
        {
            DWORD written = 0;
            const auto requested = static_cast<DWORD>(std::min<std::size_t>(count, MAXDWORD));
            if (!WriteFile(handle_, bytes, requested, &written, nullptr))
            {
                windowsError(GetLastError(), "Cannot write export file");
            }
            if (!written) { throw std::runtime_error("Export write made no progress"); }
            count -= written;
            bytes += written;
        }
    }
    std::uint64_t size()
    {
        LARGE_INTEGER value{};
        if (!GetFileSizeEx(handle_, &value)) { windowsError(GetLastError(), "Cannot measure export source"); }
        if (value.QuadPart < 0) { throw std::runtime_error("Invalid export source size"); }
        return static_cast<std::uint64_t>(value.QuadPart);
    }
    std::uint64_t position()
    {
        LARGE_INTEGER zero{};
        LARGE_INTEGER value{};
        if (!SetFilePointerEx(handle_, zero, &value, FILE_CURRENT))
        {
            windowsError(GetLastError(), "Cannot locate export position");
        }
        return static_cast<std::uint64_t>(value.QuadPart);
    }
    void rewind()
    {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN))
        {
            windowsError(GetLastError(), "Cannot rewind export file");
        }
    }
    void flush()
    {
        if (!FlushFileBuffers(handle_)) { windowsError(GetLastError(), "Cannot sync export file"); }
    }
    void close()
    {
        if (handle_ == INVALID_HANDLE_VALUE) { return; }
        const auto handle = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        if (!CloseHandle(handle)) { windowsError(GetLastError(), "Cannot close export file"); }
    }
  private:
    HANDLE handle_;
};
#else
class File
{
  public:
    explicit File(FILE *file) : file_(file)
    {
        if (!file_) { fileError("Cannot open export file"); }
    }
    explicit File(const fs::path &path)
        : File(std::fopen(path.c_str(), "rb")) {}
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    ~File() { if (file_) { std::fclose(file_); } }
    std::size_t read(void *data, std::size_t capacity)
    {
        const auto count = std::fread(data, 1, capacity, file_);
        if (std::ferror(file_)) { fileError("Cannot read export file"); }
        return count;
    }
    void write(const void *data, std::size_t count)
    {
        if (count && std::fwrite(data, 1, count, file_) != count)
        {
            fileError("Cannot write export file");
        }
    }
    std::uint64_t position()
    {
        const auto result = ftello(file_);

        if (result < 0) { fileError("Cannot locate export position"); }
        return static_cast<std::uint64_t>(result);
    }
    std::uint64_t size()
    {
        struct stat info{};
        if (fstat(fileno(file_), &info) != 0) { fileError("Cannot measure export source"); }
        if (info.st_size < 0) { throw std::runtime_error("Invalid export source size"); }
        return static_cast<std::uint64_t>(info.st_size);
    }
    void rewind()
    {
        const auto result = fseeko(file_, 0, SEEK_SET);

        if (result) { fileError("Cannot rewind export file"); }
    }
    void close()
    {
        if (!file_) { return; }
        auto *file = file_;
        file_ = nullptr;
        if (std::fclose(file)) { fileError("Cannot close export file"); }
    }
    void flush()
    {
        if (std::fflush(file_)) { fileError("Cannot flush export file"); }
        if (fsync(fileno(file_))) { fileError("Cannot sync export file"); }

    }
  private:
    FILE *file_;
};
#endif
class Temporary
{
  public:
    explicit Temporary(const fs::path &directory)
    {
        static std::atomic<std::uint64_t> counter{0};
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = directory / ("workbench-output-" + std::to_string(stamp) + "-" +
                std::to_string(counter.fetch_add(1)) + ".tmp");
#ifdef _WIN32
            const auto handle = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                const auto error = GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) { continue; }
                windowsError(error, "Cannot create export temporary");
            }
            file_.emplace(handle);
#else
            const int descriptor = open(path_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
            if (descriptor < 0)
            {
                if (errno == EEXIST) { continue; }
                fileError("Cannot create export temporary");
            }
            auto *stream = fdopen(descriptor, "w+b");
            if (!stream)
            {
                const int error = errno;
                ::close(descriptor);
                removeTemporary(path_);
                errno = error;
                fileError("Cannot open export temporary stream");
            }
            file_.emplace(stream);
#endif
            return;
        }
        throw std::runtime_error("Cannot reserve an export temporary name");
    }
    ~Temporary()
    {
        file_.reset();
        removeTemporary(path_);
    }
    File &file() { return *file_; }
    void close() { file_->flush(); file_->close(); }
    const fs::path &path() const { return path_; }
  private:
    fs::path path_;
    std::optional<File> file_;
};
#ifdef _WIN32
void distinctFiles(const fs::path &source, const fs::path &target)
{
    if (source.native() == target.native())
    {
        throw std::invalid_argument("Export source and target must be different files");
    }
    const auto openIdentity = [](const fs::path &path)
    {
        return CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    };
    const auto first = openIdentity(source);
    if (first == INVALID_HANDLE_VALUE) { windowsError(GetLastError(), "Cannot identify export source"); }
    File sourceOwner(first);
    const auto second = openIdentity(target);
    if (second == INVALID_HANDLE_VALUE)
    {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) { return; }
        windowsError(error, "Cannot identify export target");
    }
    File targetOwner(second);
    BY_HANDLE_FILE_INFORMATION a{};
    BY_HANDLE_FILE_INFORMATION b{};
    if (!GetFileInformationByHandle(first, &a) || !GetFileInformationByHandle(second, &b))
    {
        windowsError(GetLastError(), "Cannot identify export files");
    }
    // Some remote providers return a zero file index; do not mistake all their
    // different files for aliases. Regular file identities compare volume+index.
    if ((a.nFileIndexHigh || a.nFileIndexLow) &&
        a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
        a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow)
    {
        throw std::invalid_argument("Export source and target must be different files");
    }
}
#else
void distinctFiles(const fs::path &source, const fs::path &target)
{
    std::error_code error;
    if (fs::weakly_canonical(source) == fs::weakly_canonical(target) ||
        fs::equivalent(source, target, error))
    {
        throw std::invalid_argument("Export source and target must be different files");
    }
}
#endif
void copy(File &source, File &target, const OutputControl &control)
{
    std::array<unsigned char, chunkSize> buffer;
    for (;;)
    {
        checkpoint(control);
        const auto count = source.read(buffer.data(), buffer.size());
        if (!count) { break; }
        target.write(buffer.data(), count);
    }
}
// Return false only for a different filesystem. Other failures must not be
// hidden by a copy fallback, particularly permission or existing-directory errors.
bool atomicMove(const fs::path &source, const fs::path &target)
{
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return true;
    }
    const auto error = GetLastError();
    if (error == ERROR_NOT_SAME_DEVICE) { return false; }
    throw std::system_error(error, std::system_category(), "Cannot publish export");
#else
    if (::rename(source.c_str(), target.c_str()) == 0) { return true; }
    if (errno == EXDEV) { return false; }
    fileError("Cannot publish export");
#endif
}
void put(File &file, std::uint64_t value, unsigned count)
{
    std::array<unsigned char, 8> bytes{};
    for (unsigned index = 0; index < count; ++index)
    {
        bytes[index] = static_cast<unsigned char>(value >> (8 * index));
    }
    file.write(bytes.data(), count);
}
void localHeader(File &file, std::uint32_t crc, std::uint64_t size, std::uint64_t packed)
{
    put(file, 0x04034b50, 4);
    put(file, 45, 2);
    put(file, 0x800, 2);
    put(file, 8, 2);
    put(file, 0, 4);
    put(file, crc, 4);
    put(file, 0xffffffff, 4);
    put(file, 0xffffffff, 4);
    put(file, 10, 2);
    put(file, 20, 2);
    file.write("record.mrl", 10);
    put(file, 1, 2);
    put(file, 16, 2);
    put(file, size, 8);
    put(file, packed, 8);
}
void zipEnd(File &file, std::uint32_t crc, std::uint64_t size, std::uint64_t packed)
{
    const auto central = file.position();
    put(file, 0x02014b50, 4);
    put(file, 45, 2);
    put(file, 45, 2);
    put(file, 0x800, 2);
    put(file, 8, 2);
    put(file, 0, 4);
    put(file, crc, 4);
    put(file, 0xffffffff, 4);
    put(file, 0xffffffff, 4);
    put(file, 10, 2);
    put(file, 28, 2);
    put(file, 0, 2);
    put(file, 0, 2);
    put(file, 0, 2);
    put(file, 0, 4);
    put(file, 0xffffffff, 4);
    file.write("record.mrl", 10);
    put(file, 1, 2);
    put(file, 24, 2);
    put(file, size, 8);
    put(file, packed, 8);
    put(file, 0, 8);
    const auto end = file.position();
    put(file, 0x06064b50, 4);
    put(file, 44, 8);
    put(file, 45, 2);
    put(file, 45, 2);
    put(file, 0, 8);
    put(file, 1, 8);
    put(file, 1, 8);
    put(file, end - central, 8);
    put(file, central, 8);
    put(file, 0x07064b50, 4);
    put(file, 0, 4);
    put(file, end, 8);
    put(file, 1, 4);
    put(file, 0x06054b50, 4);
    put(file, 0, 4);
    put(file, 65535, 2);
    put(file, 65535, 2);
    put(file, 0xffffffff, 4);
    put(file, 0xffffffff, 4);
    put(file, 0, 2);
}
class Deflater
{
  public:
    Deflater(File &output, bool gzip, const OutputControl &control)
        : output_(output), control_(control)
    {
        int windowBits = -15;
        if (gzip)
        {
            windowBits = 31;
        }
        const auto status = deflateInit2(&stream_, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
            windowBits, 8, Z_DEFAULT_STRATEGY);
        if (status != Z_OK) { throw std::runtime_error("Cannot initialize DEFLATE"); }
        if (gzip)
        {
            header_.os = 255;
            if (deflateSetHeader(&stream_, &header_) != Z_OK)
            {
                deflateEnd(&stream_);
                throw std::runtime_error("Cannot initialize GZIP header");
            }
        }
    }
    ~Deflater() { deflateEnd(&stream_); }
    Deflater(const Deflater &) = delete;
    Deflater &operator=(const Deflater &) = delete;
    void write(const void *data, std::size_t count)
    {
        stream_.next_in = reinterpret_cast<Bytef *>(const_cast<void *>(data));
        stream_.avail_in = static_cast<uInt>(count);
        while (stream_.avail_in) { pump(Z_NO_FLUSH); }
    }
    void finish()
    {
        stream_.next_in = nullptr;
        stream_.avail_in = 0;
        while (pump(Z_FINISH) != Z_STREAM_END) {}
    }
  private:
    int pump(int flush)
    {
        checkpoint(control_);
        stream_.next_out = buffer_.data();
        stream_.avail_out = static_cast<uInt>(buffer_.size());
        const auto status = deflate(&stream_, flush);
        if (status != Z_OK && status != Z_STREAM_END)
        {
            throw std::runtime_error("DEFLATE export failed");
        }
        output_.write(buffer_.data(), buffer_.size() - stream_.avail_out);
        return status;
    }
    File &output_;
    const OutputControl &control_;
    z_stream stream_{};
    gz_header header_{};
    std::array<Bytef, chunkSize> buffer_;
};
}

void publishOutput(const fs::path &source, const fs::path &target, const OutputControl &control)
{
    const auto input = absolutePath(source);
    const auto output = absolutePath(target);
    distinctFiles(input, output);
    checkpoint(control);
    if (atomicMove(input, output)) { return; }
    Temporary publication(output.parent_path());
    {
        File file(input);
        copy(file, publication.file(), control);
    }
    publication.close();
    checkpoint(control);
    if (!atomicMove(publication.path(), output))
    {
        throw std::runtime_error("Publication temporary is on a different filesystem");
    }
    // The source is still owned by the caller after a cross-volume publication.
    // finalizeOutput's RAII owner or the WAV caller removes it after success.
}

void finalizeOutput(const fs::path &source, const fs::path &target,
    OutputFormat format, const std::string &title, const OutputControl &control)
{
    if (format > OutputFormat::Mrz) { throw std::invalid_argument("Unknown output format"); }
    const auto input = absolutePath(source);
    const auto output = absolutePath(target);
    distinctFiles(input, output);
    checkpoint(control);
    File in(input);
    const auto inputSize = in.size();
    std::vector<std::uint8_t> header;
    std::vector<std::uint8_t> gd3;
    const bool vgm = format == OutputFormat::Vgm || format == OutputFormat::Vgz;
    if (vgm)
    {
        header.resize(0x40);
        if (in.read(header.data(), header.size()) != header.size())
        {
            throw std::runtime_error("Invalid VGM header");
        }
        auto tagged = tagVgm(header, title);
        gd3.assign(tagged.begin() + 0x40, tagged.end());
        if (inputSize + gd3.size() - 4 > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("VGM output exceeds 32-bit format offsets");
        }
        const auto patch = [&](std::size_t offset, std::uint64_t value)
        {
            for (unsigned i = 0; i < 4; ++i) { header[offset + i] = static_cast<std::uint8_t>(value >> (i * 8)); }
        };
        patch(4, inputSize + gd3.size() - 4);
        patch(0x14, inputSize - 0x14);
    }
    Temporary stage(absolutePath(currentDirectory()));
    const bool zip = format == OutputFormat::Mrz;
    if (zip) { localHeader(stage.file(), 0, 0, 0); }
    const auto packedStart = stage.file().position();
    std::optional<Deflater> compressor;
    if (zip || format == OutputFormat::Vgz) { compressor.emplace(stage.file(), !zip, control); }
    const auto write = [&](const void *data, std::size_t count)
    {
        checkpoint(control);
        if (compressor) { compressor->write(data, count); }
        else { stage.file().write(data, count); }
    };
    std::uint64_t total = 0;
    auto crc = crc32(0, nullptr, 0);
    if (vgm) { write(header.data(), header.size()); total += header.size(); }
    std::array<unsigned char, chunkSize> buffer;
    for (;;)
    {
        checkpoint(control);
        const auto count = in.read(buffer.data(), buffer.size());
        if (!count) { break; }
        write(buffer.data(), count);
        if (zip) { crc = crc32(crc, buffer.data(), static_cast<uInt>(count)); }
        total += count;
        long double fraction = 1;
        if (inputSize)
        {
            fraction = static_cast<long double>(total) / inputSize;
        }
        report(control, 950 + static_cast<int>(std::min<long double>(1, fraction) * 39));
    }
    if (total != inputSize) { throw std::runtime_error("Export input changed during reading"); }
    if (vgm)
    {
        for (std::size_t offset = 0; offset < gd3.size(); offset += chunkSize)
        {
            write(gd3.data() + offset, std::min(chunkSize, gd3.size() - offset));
        }
    }
    if (compressor) { compressor->finish(); }
    compressor.reset();
    if (zip)
    {
        const auto packed = stage.file().position() - packedStart;
        zipEnd(stage.file(), crc, total, packed);
        stage.file().rewind();
        localHeader(stage.file(), crc, total, packed);
    }
    in.close();
    stage.close();
    report(control, 990);
    publishOutput(stage.path(), output, control);
    // Progress is observational after publication; reporting must not turn a
    // completed output into an apparent failure if a client callback throws.
    try { report(control, 1000); } catch (...) {}
}
}
