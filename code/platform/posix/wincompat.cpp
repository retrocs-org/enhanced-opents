/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The Win32 functions declared in wincompat.h, built on POSIX and the C++ library.

#include "wincompat.h"
#include "posixpaths.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fenv.h>
#include <filesystem>
#include <fnmatch.h>
#include <dirent.h>
#include <string>
#include <thread>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <utime.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <pthread.h>
#else
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#endif

namespace {

thread_local DWORD LastError = 0;

DWORD Error_From_Errno(int error)
{
	switch (error) {
		case 0:			return ERROR_SUCCESS;
		case ENOENT:	return ERROR_FILE_NOT_FOUND;
		case ENOTDIR:	return ERROR_PATH_NOT_FOUND;
		case EACCES:
		case EPERM:		return ERROR_ACCESS_DENIED;
		case EBADF:		return ERROR_INVALID_HANDLE;
		case EEXIST:	return ERROR_ALREADY_EXISTS;
		default:		return 0x10000 + (DWORD)error;
	}
}

BOOL Fail_Errno(void)
{
	LastError = Error_From_Errno(errno);
	return FALSE;
}

std::chrono::steady_clock::time_point const ProcessStart = std::chrono::steady_clock::now();

// Paths reach these functions in Windows form; the drive prefix and the separators are fixed
// up and the case of each part is matched against the disk, since data files on a
// case-sensitive volume rarely match the case the game asks for.
std::string Native_Path(char const * path)
{
	std::string result = path != nullptr ? path : "";
	std::replace(result.begin(), result.end(), '\\', '/');
	if (result.size() >= 2 && result[1] == ':' && isalpha((unsigned char)result[0])) {
		result.erase(0, 2);
	}
	return result;
}

std::string Resolve_Case(std::string const & path)
{
	struct stat info;
	if (path.empty() || lstat(path.c_str(), &info) == 0) {
		return path;
	}

#ifdef _PC_CASE_SENSITIVE
	auto const slash = path.find_last_of('/');
	std::string const parent_path = slash == std::string::npos ? "." : path.substr(0, std::max(std::size_t{1}, slash));
	if (pathconf(parent_path.c_str(), _PC_CASE_SENSITIVE) == 0) {
		return path;
	}
#endif

	std::string resolved;
	size_t start = 0;
	if (path[0] == '/') {
		resolved = "/";
		start = 1;
	}

	while (start <= path.size()) {
		size_t const slash = path.find('/', start);
		std::string const part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		std::string const parent = resolved.empty() ? "." : resolved;
		std::string chosen = part;

		if (!part.empty() && part != "." && part != "..") {
			std::string const candidate = (resolved.empty() ? "" : resolved) + part;
			if (lstat(candidate.c_str(), &info) != 0
#ifdef _PC_CASE_SENSITIVE
				&& pathconf(parent.c_str(), _PC_CASE_SENSITIVE) != 0
#endif
			) {
				if (DIR * dir = opendir(parent.c_str())) {
					while (dirent * entry = readdir(dir)) {
						if (strcasecmp(entry->d_name, part.c_str()) == 0) {
							chosen = entry->d_name;
							break;
						}
					}
					closedir(dir);
				}
			}
		}

		resolved += chosen;
		if (slash == std::string::npos) break;
		resolved += '/';
		start = slash + 1;
	}
	return resolved;
}

std::string Disk_Path(char const * path)
{
	return Resolve_Case(Native_Path(path));
}

// FILETIME counts 100 ns intervals from 1601; Unix time counts seconds from 1970.
constexpr long long FileTimeEpochOffset = 116444736000000000LL;

ULONGLONG File_Time_Value(FILETIME const & time)
{
	return ((ULONGLONG)time.dwHighDateTime << 32) | time.dwLowDateTime;
}

FILETIME To_File_Time(ULONGLONG value)
{
	FILETIME time;
	time.dwLowDateTime = (DWORD)value;
	time.dwHighDateTime = (DWORD)(value >> 32);
	return time;
}

FILETIME From_Timespec(time_t seconds, long nanoseconds)
{
	return To_File_Time((ULONGLONG)((long long)seconds * 10000000LL + nanoseconds / 100 + FileTimeEpochOffset));
}

void Split_Time(FILETIME const * file, time_t & seconds, long & nanoseconds)
{
	long long value = (long long)File_Time_Value(*file) - FileTimeEpochOffset;
	seconds = (time_t)(value / 10000000LL);
	nanoseconds = (long)((value % 10000000LL) * 100);
	if (nanoseconds < 0) {
		seconds -= 1;
		nanoseconds += 1000000000L;
	}
}

void Fill_From_Stat(struct stat const & info, char const * name, DWORD & attributes, FILETIME & created,
	FILETIME & accessed, FILETIME & written, DWORD & sizehigh, DWORD & sizelow)
{
	attributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
	if ((info.st_mode & S_IWUSR) == 0) attributes |= FILE_ATTRIBUTE_READONLY;
	if (name != nullptr && name[0] == '.') attributes |= FILE_ATTRIBUTE_HIDDEN;
#if defined(__APPLE__)
	written = From_Timespec(info.st_mtimespec.tv_sec, info.st_mtimespec.tv_nsec);
	accessed = From_Timespec(info.st_atimespec.tv_sec, info.st_atimespec.tv_nsec);
	created = From_Timespec(info.st_birthtimespec.tv_sec, info.st_birthtimespec.tv_nsec);
#else
	written = From_Timespec(info.st_mtim.tv_sec, info.st_mtim.tv_nsec);
	accessed = From_Timespec(info.st_atim.tv_sec, info.st_atim.tv_nsec);
	created = From_Timespec(info.st_ctim.tv_sec, info.st_ctim.tv_nsec);
#endif
	sizehigh = (DWORD)((uint64_t)info.st_size >> 32);
	sizelow = (DWORD)info.st_size;
}

struct FileHandle
{
	uint32_t Magic;
	int Descriptor;
};
constexpr uint32_t FileMagic = 0x46494C45;

struct FindHandle
{
	uint32_t Magic;
	DIR * Directory;
	std::string Folder;
	std::string Pattern;
};
constexpr uint32_t FindMagic = 0x46494E44;

FileHandle * As_File(HANDLE handle)
{
	FileHandle * file = (FileHandle *)handle;
	return (file != nullptr && handle != INVALID_HANDLE_VALUE && file->Magic == FileMagic) ? file : nullptr;
}

bool Find_Next(FindHandle * find, WIN32_FIND_DATAA * data)
{
	while (dirent * entry = readdir(find->Directory)) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
		if (fnmatch(find->Pattern.c_str(), entry->d_name, FNM_CASEFOLD) != 0) continue;

		std::string const full = find->Folder + entry->d_name;
		struct stat info;
		if (stat(full.c_str(), &info) != 0) continue;

		memset(data, 0, sizeof(*data));
		Fill_From_Stat(info, entry->d_name, data->dwFileAttributes, data->ftCreationTime,
			data->ftLastAccessTime, data->ftLastWriteTime, data->nFileSizeHigh, data->nFileSizeLow);
		snprintf(data->cFileName, sizeof(data->cFileName), "%s", entry->d_name);
		return true;
	}
	return false;
}

// UTF-8 to code points and back, for the wide string conversions.
int Decode_UTF8(char const * text, int length, wchar_t * out, int room)
{
	int count = 0;
	int i = 0;
	while (i < length) {
		unsigned char c = (unsigned char)text[i];
		unsigned code = c;
		int extra = 0;
		if (c >= 0xF0) { extra = 3; code = c & 0x07; }
		else if (c >= 0xE0) { extra = 2; code = c & 0x0F; }
		else if (c >= 0xC0) { extra = 1; code = c & 0x1F; }
		else if (c >= 0x80) { extra = 0; code = 0xFFFD; }
		i++;
		for (int k = 0; k < extra; k++) {
			if (i >= length || ((unsigned char)text[i] & 0xC0) != 0x80) { code = 0xFFFD; break; }
			code = (code << 6) | ((unsigned char)text[i] & 0x3F);
			i++;
		}
		if (out != nullptr) {
			if (count >= room) return 0;
			out[count] = (wchar_t)code;
		}
		count++;
	}
	return count;
}

int Encode_UTF8(wchar_t const * wide, int length, char * out, int room)
{
	int count = 0;
	for (int i = 0; i < length; i++) {
		unsigned code = (unsigned)wide[i];
		char buffer[4];
		int bytes;
		if (code < 0x80) { buffer[0] = (char)code; bytes = 1; }
		else if (code < 0x800) { buffer[0] = (char)(0xC0 | (code >> 6)); buffer[1] = (char)(0x80 | (code & 0x3F)); bytes = 2; }
		else if (code < 0x10000) { buffer[0] = (char)(0xE0 | (code >> 12)); buffer[1] = (char)(0x80 | ((code >> 6) & 0x3F)); buffer[2] = (char)(0x80 | (code & 0x3F)); bytes = 3; }
		else { buffer[0] = (char)(0xF0 | (code >> 18)); buffer[1] = (char)(0x80 | ((code >> 12) & 0x3F)); buffer[2] = (char)(0x80 | ((code >> 6) & 0x3F)); buffer[3] = (char)(0x80 | (code & 0x3F)); bytes = 4; }
		if (out != nullptr) {
			if (count + bytes > room) return 0;
			memcpy(out + count, buffer, (size_t)bytes);
		}
		count += bytes;
	}
	return count;
}

std::string Directory_Of(std::string const & path)
{
	size_t const slash = path.find_last_of('/');
	return slash == std::string::npos ? std::string("./") : path.substr(0, slash + 1);
}

}


/*
**	Paths.
*/
std::string Posix_Executable_Path(void)
{
	static std::string const path = [] {
		char buffer[4096] = {};
#if defined(__APPLE__)
		uint32_t size = sizeof(buffer);
		if (_NSGetExecutablePath(buffer, &size) != 0) {
			return std::string();
		}
#else
		ssize_t const length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if (length <= 0) {
			return std::string();
		}
		buffer[length] = '\0';
#endif
		char resolved[PATH_MAX];
		if (realpath(buffer, resolved) != nullptr) {
			return std::string(resolved);
		}
		return std::string(buffer);
	}();
	return path;
}


std::string Posix_Executable_Directory(void)
{
	return Directory_Of(Posix_Executable_Path());
}


static bool Is_In_Bundle(std::string const & executable_directory)
{
	return executable_directory.size() > 16
		&& executable_directory.compare(executable_directory.size() - 16, 16, "/Contents/MacOS/") == 0;
}


std::string Posix_Resource_Directory(void)
{
	std::string const directory = Posix_Executable_Directory();
	if (Is_In_Bundle(directory)) {
		return directory.substr(0, directory.size() - 6) + "Resources/";
	}
	return directory;
}


std::string Posix_UI_Directory(void)
{
	return Posix_Resource_Directory() + "ui/";
}


std::string Posix_Bundle_Parent_Directory(void)
{
	std::string const directory = Posix_Executable_Directory();
	if (!Is_In_Bundle(directory)) {
		return std::string();
	}
	// Strip "Contents/MacOS/", then the bundle's own name.
	std::string const bundle = directory.substr(0, directory.size() - 16);
	return Directory_Of(bundle);
}


std::string Posix_User_Directory(void)
{
	static std::string const directory = [] {
		std::string base;
		char const * home = getenv("HOME");
#if defined(__APPLE__)
		if (home != nullptr) {
			base = std::string(home) + "/Library/Application Support/OpenTS/";
		}
#else
		char const * xdg = getenv("XDG_DATA_HOME");
		if (xdg != nullptr && xdg[0] != '\0') {
			base = std::string(xdg) + "/OpenTS/";
		} else if (home != nullptr) {
			base = std::string(home) + "/.local/share/OpenTS/";
		}
#endif
		if (!base.empty()) {
			std::error_code error;
			std::filesystem::create_directories(base, error);
		}
		return base;
	}();
	return directory;
}


/*
**	Timing.
*/
DWORD timeGetTime(void)
{
	return (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


DWORD GetTickCount(void)
{
	return timeGetTime();
}


ULONGLONG GetTickCount64(void)
{
	return (ULONGLONG)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


void Sleep(DWORD milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}


BOOL QueryPerformanceCounter(LARGE_INTEGER * count)
{
	if (count == nullptr) return FALSE;
	count->QuadPart = (LONGLONG)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	return TRUE;
}


BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency)
{
	if (frequency == nullptr) return FALSE;
	frequency->QuadPart = 1000000000LL;
	return TRUE;
}


void OutputDebugStringA(char const * text)
{
	if (text != nullptr) {
		fputs(text, stderr);
	}
}


char const * Last_Error_Text(uint32_t error)
{
	if (error >= 0x10000) {
		return strerror((int)(error - 0x10000));
	}
	switch (error) {
		case ERROR_SUCCESS:			return "The operation completed successfully.";
		case ERROR_FILE_NOT_FOUND:	return "The file was not found.";
		case ERROR_PATH_NOT_FOUND:	return "The path was not found.";
		case ERROR_ACCESS_DENIED:	return "Access is denied.";
		case ERROR_INVALID_HANDLE:	return "The handle is invalid.";
		case ERROR_ALREADY_EXISTS:	return "The file already exists.";
		default:					return "Unknown error.";
	}
}


DWORD GetLastError(void)
{
	return LastError;
}


void SetLastError(DWORD error)
{
	LastError = error;
}


DWORD GetCurrentProcessId(void)
{
	return (DWORD)getpid();
}


DWORD GetCurrentThreadId(void)
{
#if defined(__APPLE__)
	uint64_t id = 0;
	pthread_threadid_np(nullptr, &id);
	return (DWORD)id;
#else
	return (DWORD)syscall(SYS_gettid);
#endif
}


// Every string the game handles is UTF-8 already.
UINT GetACP(void)
{
	return CP_UTF8;
}


static void Fill_System_Time(struct tm const & parts, long milliseconds, SYSTEMTIME * time)
{
	time->wYear = (WORD)(parts.tm_year + 1900);
	time->wMonth = (WORD)(parts.tm_mon + 1);
	time->wDayOfWeek = (WORD)parts.tm_wday;
	time->wDay = (WORD)parts.tm_mday;
	time->wHour = (WORD)parts.tm_hour;
	time->wMinute = (WORD)parts.tm_min;
	time->wSecond = (WORD)parts.tm_sec;
	time->wMilliseconds = (WORD)milliseconds;
}


void GetLocalTime(SYSTEMTIME * time)
{
	struct timeval now;
	gettimeofday(&now, nullptr);
	struct tm parts;
	time_t seconds = now.tv_sec;
	localtime_r(&seconds, &parts);
	Fill_System_Time(parts, now.tv_usec / 1000, time);
}


void GetSystemTime(SYSTEMTIME * time)
{
	struct timeval now;
	gettimeofday(&now, nullptr);
	struct tm parts;
	time_t seconds = now.tv_sec;
	gmtime_r(&seconds, &parts);
	Fill_System_Time(parts, now.tv_usec / 1000, time);
}


DWORD GetModuleFileNameA(HMODULE, char * buffer, DWORD size)
{
	if (buffer == nullptr || size == 0) return 0;
	std::string const path = Posix_Executable_Path();
	snprintf(buffer, size, "%s", path.c_str());
	return (DWORD)std::min<size_t>(path.size(), size - 1);
}


DWORD GetCurrentDirectoryA(DWORD size, char * buffer)
{
	char path[PATH_MAX];
	if (getcwd(path, sizeof(path)) == nullptr) {
		Fail_Errno();
		return 0;
	}
	size_t const length = strlen(path);
	if (buffer == nullptr || size <= length + 1) {
		return (DWORD)(length + 2);
	}
	memcpy(buffer, path, length);
	buffer[length] = '/';
	buffer[length + 1] = '\0';
	return (DWORD)(length + 1);
}


BOOL SetCurrentDirectoryA(char const * path)
{
	std::string const native = Disk_Path(path);
	return chdir(native.empty() ? "." : native.c_str()) == 0 ? TRUE : Fail_Errno();
}


BOOL CreateDirectoryA(char const * path, void *)
{
	std::string native = Native_Path(path);
	while (native.size() > 1 && native.back() == '/') native.pop_back();
	native = Resolve_Case(native);
	return mkdir(native.c_str(), 0777) == 0 ? TRUE : Fail_Errno();
}


BOOL RemoveDirectoryA(char const * path)
{
	return rmdir(Disk_Path(path).c_str()) == 0 ? TRUE : Fail_Errno();
}


BOOL DeleteFileA(char const * path)
{
	return unlink(Disk_Path(path).c_str()) == 0 ? TRUE : Fail_Errno();
}


BOOL MoveFileA(char const * from, char const * to)
{
	return MoveFileExA(from, to, 0);
}


BOOL MoveFileExA(char const * from, char const * to, DWORD flags)
{
	std::string const source = Disk_Path(from);
	std::string const dest = Disk_Path(to);
	struct stat info;
	if ((flags & MOVEFILE_REPLACE_EXISTING) == 0 && stat(dest.c_str(), &info) == 0) {
		LastError = ERROR_ALREADY_EXISTS;
		return FALSE;
	}
	if (rename(source.c_str(), dest.c_str()) == 0) {
		return TRUE;
	}
	if (errno == EXDEV && (flags & MOVEFILE_COPY_ALLOWED) != 0) {
		if (CopyFileA(from, to, FALSE) && unlink(source.c_str()) == 0) {
			return TRUE;
		}
	}
	return Fail_Errno();
}


BOOL ReplaceFileA(char const * replaced, char const * replacement, char const *, DWORD, void *, void *)
{
	return MoveFileExA(replacement, replaced, MOVEFILE_REPLACE_EXISTING);
}


BOOL CopyFileA(char const * from, char const * to, BOOL fail_if_exists)
{
	std::error_code error;
	auto const options = fail_if_exists ? std::filesystem::copy_options::none : std::filesystem::copy_options::overwrite_existing;
	if (std::filesystem::copy_file(Disk_Path(from), Disk_Path(to), options, error)) {
		return TRUE;
	}
	LastError = Error_From_Errno(error.value());
	return FALSE;
}


DWORD GetFileAttributesA(char const * path)
{
	std::string native = Native_Path(path);
	while (native.size() > 1 && native.back() == '/') native.pop_back();
	native = Resolve_Case(native);

	struct stat info;
	if (stat(native.c_str(), &info) != 0) {
		Fail_Errno();
		return INVALID_FILE_ATTRIBUTES;
	}
	DWORD attributes;
	FILETIME a, b, c;
	DWORD high, low;
	size_t const slash = native.find_last_of('/');
	Fill_From_Stat(info, native.c_str() + (slash == std::string::npos ? 0 : slash + 1), attributes, a, b, c, high, low);
	return attributes;
}


BOOL GetFileAttributesExA(char const * path, GET_FILEEX_INFO_LEVELS, void * buffer)
{
	WIN32_FILE_ATTRIBUTE_DATA * data = (WIN32_FILE_ATTRIBUTE_DATA *)buffer;
	std::string const native = Disk_Path(path);
	struct stat info;
	if (data == nullptr || stat(native.c_str(), &info) != 0) {
		return Fail_Errno();
	}
	Fill_From_Stat(info, nullptr, data->dwFileAttributes, data->ftCreationTime, data->ftLastAccessTime,
		data->ftLastWriteTime, data->nFileSizeHigh, data->nFileSizeLow);
	return TRUE;
}


BOOL GetDiskFreeSpaceExA(char const * path, ULARGE_INTEGER * available, ULARGE_INTEGER * total, ULARGE_INTEGER * free)
{
	std::string native = (path != nullptr && path[0] != '\0') ? Disk_Path(path) : std::string(".");
	struct statvfs info;
	if (statvfs(native.c_str(), &info) != 0) {
		return Fail_Errno();
	}
	if (available != nullptr) available->QuadPart = (ULONGLONG)info.f_bavail * info.f_frsize;
	if (total != nullptr) total->QuadPart = (ULONGLONG)info.f_blocks * info.f_frsize;
	if (free != nullptr) free->QuadPart = (ULONGLONG)info.f_bfree * info.f_frsize;
	return TRUE;
}


DWORD GetTempPathA(DWORD size, char * buffer)
{
	char const * temp = getenv("TMPDIR");
	std::string path = (temp != nullptr && temp[0] != '\0') ? temp : "/tmp";
	if (path.back() != '/') path += '/';
	if (buffer == nullptr || size <= path.size()) return (DWORD)(path.size() + 1);
	memcpy(buffer, path.c_str(), path.size() + 1);
	return (DWORD)path.size();
}


DWORD GetFullPathNameA(char const * name, DWORD size, char * buffer, char ** part)
{
	std::error_code error;
	std::string const path = std::filesystem::absolute(Native_Path(name), error).lexically_normal().string();
	if (buffer == nullptr || size <= path.size()) return (DWORD)(path.size() + 1);
	memcpy(buffer, path.c_str(), path.size() + 1);
	if (part != nullptr) {
		char * slash = strrchr(buffer, '/');
		*part = slash != nullptr ? slash + 1 : buffer;
	}
	return (DWORD)path.size();
}


HANDLE CreateFileA(char const * name, DWORD access, DWORD, void *, DWORD disposition, DWORD, HANDLE)
{
	int flags = 0;
	if ((access & GENERIC_READ) && (access & GENERIC_WRITE)) flags = O_RDWR;
	else if (access & GENERIC_WRITE) flags = O_WRONLY;
	else flags = O_RDONLY;

	switch (disposition) {
		case CREATE_NEW:		flags |= O_CREAT | O_EXCL; break;
		case CREATE_ALWAYS:		flags |= O_CREAT | O_TRUNC; break;
		case OPEN_ALWAYS:		flags |= O_CREAT; break;
		case TRUNCATE_EXISTING:	flags |= O_TRUNC; break;
		default:				break;
	}

	std::string const path = Disk_Path(name);
	int const descriptor = open(path.c_str(), flags | O_CLOEXEC, 0666);
	if (descriptor < 0) {
		Fail_Errno();
		return INVALID_HANDLE_VALUE;
	}

	FileHandle * file = new FileHandle();
	file->Magic = FileMagic;
	file->Descriptor = descriptor;
	LastError = ERROR_SUCCESS;
	return file;
}


BOOL ReadFile(HANDLE handle, void * buffer, DWORD count, DWORD * read_count, void *)
{
	FileHandle * file = As_File(handle);
	if (read_count != nullptr) *read_count = 0;
	if (file == nullptr) { LastError = ERROR_INVALID_HANDLE; return FALSE; }

	size_t total = 0;
	while (total < count) {
		ssize_t const got = read(file->Descriptor, (char *)buffer + total, count - total);
		if (got < 0) {
			if (errno == EINTR) continue;
			if (read_count != nullptr) *read_count = (DWORD)total;
			return Fail_Errno();
		}
		if (got == 0) break;
		total += (size_t)got;
	}
	if (read_count != nullptr) *read_count = (DWORD)total;
	return TRUE;
}


BOOL WriteFile(HANDLE handle, void const * buffer, DWORD count, DWORD * written, void *)
{
	FileHandle * file = As_File(handle);
	if (written != nullptr) *written = 0;
	if (file == nullptr) { LastError = ERROR_INVALID_HANDLE; return FALSE; }

	size_t total = 0;
	while (total < count) {
		ssize_t const put = write(file->Descriptor, (char const *)buffer + total, count - total);
		if (put < 0) {
			if (errno == EINTR) continue;
			if (written != nullptr) *written = (DWORD)total;
			return Fail_Errno();
		}
		total += (size_t)put;
	}
	if (written != nullptr) *written = (DWORD)total;
	return TRUE;
}


BOOL CloseHandle(HANDLE handle)
{
	if (FileHandle * file = As_File(handle)) {
		int const result = close(file->Descriptor);
		DWORD const error = result == 0 ? ERROR_SUCCESS : Error_From_Errno(errno);
		file->Magic = 0;
		delete file;
		if (result == 0) return TRUE;
		LastError = error;
		return FALSE;
	}
	LastError = ERROR_INVALID_HANDLE;
	return FALSE;
}


DWORD GetFileSize(HANDLE handle, DWORD * high)
{
	FileHandle * file = As_File(handle);
	struct stat info;
	if (file == nullptr || fstat(file->Descriptor, &info) != 0) {
		Fail_Errno();
		return INVALID_FILE_SIZE;
	}
	if (high != nullptr) *high = (DWORD)((uint64_t)info.st_size >> 32);
	return (DWORD)info.st_size;
}


BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER * size)
{
	FileHandle * file = As_File(handle);
	struct stat info;
	if (file == nullptr || size == nullptr || fstat(file->Descriptor, &info) != 0) {
		return Fail_Errno();
	}
	size->QuadPart = info.st_size;
	return TRUE;
}


DWORD SetFilePointer(HANDLE handle, LONG distance, LONG * high, DWORD method)
{
	FileHandle * file = As_File(handle);
	if (file == nullptr) {
		LastError = ERROR_INVALID_HANDLE;
		return INVALID_SET_FILE_POINTER;
	}
	off_t offset = high != nullptr ? (off_t)(((int64_t)*high << 32) | (uint32_t)distance) : (off_t)distance;
	int const whence = method == FILE_CURRENT ? SEEK_CUR : method == FILE_END ? SEEK_END : SEEK_SET;
	off_t const result = lseek(file->Descriptor, offset, whence);
	if (result < 0) {
		Fail_Errno();
		return INVALID_SET_FILE_POINTER;
	}
	if (high != nullptr) *high = (LONG)((int64_t)result >> 32);
	return (DWORD)result;
}


BOOL SetEndOfFile(HANDLE handle)
{
	FileHandle * file = As_File(handle);
	if (file == nullptr) return FALSE;
	off_t const position = lseek(file->Descriptor, 0, SEEK_CUR);
	return (position >= 0 && ftruncate(file->Descriptor, position) == 0) ? TRUE : Fail_Errno();
}


BOOL FlushFileBuffers(HANDLE handle)
{
	FileHandle * file = As_File(handle);
	if (file == nullptr) return FALSE;
	return fsync(file->Descriptor) == 0 ? TRUE : Fail_Errno();
}


HANDLE FindFirstFileA(char const * pattern, WIN32_FIND_DATAA * data)
{
	std::string const native = Native_Path(pattern);
	size_t const slash = native.find_last_of('/');
	std::string folder = slash == std::string::npos ? std::string() : native.substr(0, slash + 1);
	std::string const mask = slash == std::string::npos ? native : native.substr(slash + 1);
	folder = Resolve_Case(folder);

	DIR * directory = opendir(folder.empty() ? "." : folder.c_str());
	if (directory == nullptr) {
		Fail_Errno();
		return INVALID_HANDLE_VALUE;
	}

	FindHandle * find = new FindHandle();
	find->Magic = FindMagic;
	find->Directory = directory;
	find->Folder = folder;
	// "*.*" matches every name on Windows, including those without a dot.
	find->Pattern = (mask == "*.*") ? std::string("*") : mask;

	if (!Find_Next(find, data)) {
		closedir(directory);
		find->Magic = 0;
		delete find;
		LastError = ERROR_FILE_NOT_FOUND;
		return INVALID_HANDLE_VALUE;
	}
	return find;
}


BOOL FindNextFileA(HANDLE handle, WIN32_FIND_DATAA * data)
{
	FindHandle * find = (FindHandle *)handle;
	if (find == nullptr || handle == INVALID_HANDLE_VALUE || find->Magic != FindMagic) {
		LastError = ERROR_INVALID_HANDLE;
		return FALSE;
	}
	if (!Find_Next(find, data)) {
		LastError = ERROR_NO_MORE_FILES;
		return FALSE;
	}
	return TRUE;
}


BOOL FindClose(HANDLE handle)
{
	FindHandle * find = (FindHandle *)handle;
	if (find == nullptr || handle == INVALID_HANDLE_VALUE || find->Magic != FindMagic) {
		return FALSE;
	}
	closedir(find->Directory);
	find->Magic = 0;
	delete find;
	return TRUE;
}


long filelength(int fd)
{
	struct stat info;
	if (fstat(fd, &info) != 0) return -1;
	return (long)info.st_size;
}


/*
**	Time conversion.
*/
BOOL SystemTimeToFileTime(SYSTEMTIME const * system, FILETIME * file)
{
	if (system == nullptr || file == nullptr) return FALSE;
	struct tm parts = {};
	parts.tm_year = system->wYear - 1900;
	parts.tm_mon = system->wMonth - 1;
	parts.tm_mday = system->wDay;
	parts.tm_hour = system->wHour;
	parts.tm_min = system->wMinute;
	parts.tm_sec = system->wSecond;
	time_t const seconds = timegm(&parts);
	*file = From_Timespec(seconds, (long)system->wMilliseconds * 1000000L);
	return TRUE;
}


BOOL FileTimeToSystemTime(FILETIME const * file, SYSTEMTIME * system)
{
	if (file == nullptr || system == nullptr) return FALSE;
	time_t seconds;
	long nanoseconds;
	Split_Time(file, seconds, nanoseconds);
	struct tm parts;
	gmtime_r(&seconds, &parts);
	Fill_System_Time(parts, nanoseconds / 1000000L, system);
	return TRUE;
}


BOOL FileTimeToLocalFileTime(FILETIME const * file, FILETIME * local)
{
	if (file == nullptr || local == nullptr) return FALSE;
	time_t seconds;
	long nanoseconds;
	Split_Time(file, seconds, nanoseconds);
	struct tm parts;
	localtime_r(&seconds, &parts);
	*local = To_File_Time(File_Time_Value(*file) + (ULONGLONG)((long long)parts.tm_gmtoff * 10000000LL));
	return TRUE;
}


BOOL LocalFileTimeToFileTime(FILETIME const * local, FILETIME * file)
{
	if (file == nullptr || local == nullptr) return FALSE;
	time_t seconds;
	long nanoseconds;
	Split_Time(local, seconds, nanoseconds);
	struct tm parts;
	localtime_r(&seconds, &parts);
	*file = To_File_Time(File_Time_Value(*local) - (ULONGLONG)((long long)parts.tm_gmtoff * 10000000LL));
	return TRUE;
}


void GetSystemTimeAsFileTime(FILETIME * file)
{
	struct timeval now;
	gettimeofday(&now, nullptr);
	*file = From_Timespec(now.tv_sec, now.tv_usec * 1000L);
}


LONG CompareFileTime(FILETIME const * a, FILETIME const * b)
{
	ULONGLONG const left = File_Time_Value(*a);
	ULONGLONG const right = File_Time_Value(*b);
	return left < right ? -1 : (left > right ? 1 : 0);
}


BOOL FileTimeToDosDateTime(FILETIME const * file, WORD * date, WORD * time)
{
	SYSTEMTIME system;
	FILETIME local;
	if (!FileTimeToLocalFileTime(file, &local) || !FileTimeToSystemTime(&local, &system)) return FALSE;
	if (date != nullptr) *date = (WORD)(((system.wYear - 1980) << 9) | (system.wMonth << 5) | system.wDay);
	if (time != nullptr) *time = (WORD)((system.wHour << 11) | (system.wMinute << 5) | (system.wSecond / 2));
	return TRUE;
}


BOOL DosDateTimeToFileTime(WORD date, WORD time, FILETIME * file)
{
	SYSTEMTIME system = {};
	system.wYear = (WORD)((date >> 9) + 1980);
	system.wMonth = (WORD)((date >> 5) & 15);
	system.wDay = (WORD)(date & 31);
	system.wHour = (WORD)(time >> 11);
	system.wMinute = (WORD)((time >> 5) & 63);
	system.wSecond = (WORD)((time & 31) * 2);
	FILETIME local;
	return SystemTimeToFileTime(&system, &local) && LocalFileTimeToFileTime(&local, file);
}


static int Format_Time(SYSTEMTIME const * time, char const * format, char * buffer, int size)
{
	SYSTEMTIME now;
	if (time == nullptr) {
		GetLocalTime(&now);
		time = &now;
	}
	struct tm parts = {};
	parts.tm_year = time->wYear - 1900;
	parts.tm_mon = time->wMonth - 1;
	parts.tm_mday = time->wDay;
	parts.tm_hour = time->wHour;
	parts.tm_min = time->wMinute;
	parts.tm_sec = time->wSecond;
	parts.tm_wday = time->wDayOfWeek;
	char text[128];
	size_t const length = strftime(text, sizeof(text), format, &parts);
	if (buffer == nullptr || size == 0) return (int)length + 1;
	if ((int)length >= size) return 0;
	memcpy(buffer, text, length + 1);
	return (int)length + 1;
}


int GetDateFormatA(DWORD, DWORD flags, SYSTEMTIME const * time, char const *, char * buffer, int size)
{
	return Format_Time(time, (flags & DATE_LONGDATE) ? "%A, %B %e, %Y" : "%x", buffer, size);
}


int GetTimeFormatA(DWORD, DWORD flags, SYSTEMTIME const * time, char const *, char * buffer, int size)
{
	char const * format = "%X";
	if (flags & TIME_NOMINUTESORSECONDS) format = "%H";
	else if (flags & TIME_NOSECONDS) format = "%H:%M";
	return Format_Time(time, format, buffer, size);
}


/*
**	Text conversion. CP_UTF8 and CP_ACP give UTF-8; 437 and 1252 are kept for the bitmap fonts.
*/
namespace {

// The code points at bytes 0x80 to 0xFF of the two code pages the engine's bitmap fonts use.
char32_t const CodePage437High[128] = { 0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0 };
char32_t const CodePage1252High[128] = { 0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178, 0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF, 0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF, 0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF, 0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF, 0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF };

// What Windows' best-fit tables do for the letters the code pages lack: drop the accent.
struct BestFit { char32_t Code; char Base; };
BestFit const LatinBestFit[] = { { 0x00C0, 'A' }, { 0x00C1, 'A' }, { 0x00C2, 'A' }, { 0x00C3, 'A' }, { 0x00C4, 'A' }, { 0x00C5, 'A' }, { 0x00C7, 'C' }, { 0x00C8, 'E' }, { 0x00C9, 'E' }, { 0x00CA, 'E' }, { 0x00CB, 'E' }, { 0x00CC, 'I' }, { 0x00CD, 'I' }, { 0x00CE, 'I' }, { 0x00CF, 'I' }, { 0x00D1, 'N' }, { 0x00D2, 'O' }, { 0x00D3, 'O' }, { 0x00D4, 'O' }, { 0x00D5, 'O' }, { 0x00D6, 'O' }, { 0x00D9, 'U' }, { 0x00DA, 'U' }, { 0x00DB, 'U' }, { 0x00DC, 'U' }, { 0x00DD, 'Y' }, { 0x00E0, 'a' }, { 0x00E1, 'a' }, { 0x00E2, 'a' }, { 0x00E3, 'a' }, { 0x00E4, 'a' }, { 0x00E5, 'a' }, { 0x00E7, 'c' }, { 0x00E8, 'e' }, { 0x00E9, 'e' }, { 0x00EA, 'e' }, { 0x00EB, 'e' }, { 0x00EC, 'i' }, { 0x00ED, 'i' }, { 0x00EE, 'i' }, { 0x00EF, 'i' }, { 0x00F1, 'n' }, { 0x00F2, 'o' }, { 0x00F3, 'o' }, { 0x00F4, 'o' }, { 0x00F5, 'o' }, { 0x00F6, 'o' }, { 0x00F9, 'u' }, { 0x00FA, 'u' }, { 0x00FB, 'u' }, { 0x00FC, 'u' }, { 0x00FD, 'y' }, { 0x00FF, 'y' }, { 0x0100, 'A' }, { 0x0101, 'a' }, { 0x0102, 'A' }, { 0x0103, 'a' }, { 0x0104, 'A' }, { 0x0105, 'a' }, { 0x0106, 'C' }, { 0x0107, 'c' }, { 0x0108, 'C' }, { 0x0109, 'c' }, { 0x010A, 'C' }, { 0x010B, 'c' }, { 0x010C, 'C' }, { 0x010D, 'c' }, { 0x010E, 'D' }, { 0x010F, 'd' }, { 0x0112, 'E' }, { 0x0113, 'e' }, { 0x0114, 'E' }, { 0x0115, 'e' }, { 0x0116, 'E' }, { 0x0117, 'e' }, { 0x0118, 'E' }, { 0x0119, 'e' }, { 0x011A, 'E' }, { 0x011B, 'e' }, { 0x011C, 'G' }, { 0x011D, 'g' }, { 0x011E, 'G' }, { 0x011F, 'g' }, { 0x0120, 'G' }, { 0x0121, 'g' }, { 0x0122, 'G' }, { 0x0123, 'g' }, { 0x0124, 'H' }, { 0x0125, 'h' }, { 0x0128, 'I' }, { 0x0129, 'i' }, { 0x012A, 'I' }, { 0x012B, 'i' }, { 0x012C, 'I' }, { 0x012D, 'i' }, { 0x012E, 'I' }, { 0x012F, 'i' }, { 0x0130, 'I' }, { 0x0134, 'J' }, { 0x0135, 'j' }, { 0x0136, 'K' }, { 0x0137, 'k' }, { 0x0139, 'L' }, { 0x013A, 'l' }, { 0x013B, 'L' }, { 0x013C, 'l' }, { 0x013D, 'L' }, { 0x013E, 'l' }, { 0x0143, 'N' }, { 0x0144, 'n' }, { 0x0145, 'N' }, { 0x0146, 'n' }, { 0x0147, 'N' }, { 0x0148, 'n' }, { 0x014C, 'O' }, { 0x014D, 'o' }, { 0x014E, 'O' }, { 0x014F, 'o' }, { 0x0150, 'O' }, { 0x0151, 'o' }, { 0x0154, 'R' }, { 0x0155, 'r' }, { 0x0156, 'R' }, { 0x0157, 'r' }, { 0x0158, 'R' }, { 0x0159, 'r' }, { 0x015A, 'S' }, { 0x015B, 's' }, { 0x015C, 'S' }, { 0x015D, 's' }, { 0x015E, 'S' }, { 0x015F, 's' }, { 0x0160, 'S' }, { 0x0161, 's' }, { 0x0162, 'T' }, { 0x0163, 't' }, { 0x0164, 'T' }, { 0x0165, 't' }, { 0x0168, 'U' }, { 0x0169, 'u' }, { 0x016A, 'U' }, { 0x016B, 'u' }, { 0x016C, 'U' }, { 0x016D, 'u' }, { 0x016E, 'U' }, { 0x016F, 'u' }, { 0x0170, 'U' }, { 0x0171, 'u' }, { 0x0172, 'U' }, { 0x0173, 'u' }, { 0x0174, 'W' }, { 0x0175, 'w' }, { 0x0176, 'Y' }, { 0x0177, 'y' }, { 0x0178, 'Y' }, { 0x0179, 'Z' }, { 0x017A, 'z' }, { 0x017B, 'Z' }, { 0x017C, 'z' }, { 0x017D, 'Z' }, { 0x017E, 'z' }, { 0x01A0, 'O' }, { 0x01A1, 'o' }, { 0x01AF, 'U' }, { 0x01B0, 'u' }, { 0x01CD, 'A' }, { 0x01CE, 'a' }, { 0x01CF, 'I' }, { 0x01D0, 'i' }, { 0x01D1, 'O' }, { 0x01D2, 'o' }, { 0x01D3, 'U' }, { 0x01D4, 'u' }, { 0x01D5, 'U' }, { 0x01D6, 'u' }, { 0x01D7, 'U' }, { 0x01D8, 'u' }, { 0x01D9, 'U' }, { 0x01DA, 'u' }, { 0x01DB, 'U' }, { 0x01DC, 'u' }, { 0x01DE, 'A' }, { 0x01DF, 'a' }, { 0x01E0, 'A' }, { 0x01E1, 'a' }, { 0x01E6, 'G' }, { 0x01E7, 'g' }, { 0x01E8, 'K' }, { 0x01E9, 'k' }, { 0x01EA, 'O' }, { 0x01EB, 'o' }, { 0x01EC, 'O' }, { 0x01ED, 'o' }, { 0x01F0, 'j' }, { 0x01F4, 'G' }, { 0x01F5, 'g' }, { 0x01F8, 'N' }, { 0x01F9, 'n' }, { 0x01FA, 'A' }, { 0x01FB, 'a' }, { 0x0200, 'A' }, { 0x0201, 'a' }, { 0x0202, 'A' }, { 0x0203, 'a' }, { 0x0204, 'E' }, { 0x0205, 'e' }, { 0x0206, 'E' }, { 0x0207, 'e' }, { 0x0208, 'I' }, { 0x0209, 'i' }, { 0x020A, 'I' }, { 0x020B, 'i' }, { 0x020C, 'O' }, { 0x020D, 'o' }, { 0x020E, 'O' }, { 0x020F, 'o' }, { 0x0210, 'R' }, { 0x0211, 'r' }, { 0x0212, 'R' }, { 0x0213, 'r' }, { 0x0214, 'U' }, { 0x0215, 'u' }, { 0x0216, 'U' }, { 0x0217, 'u' }, { 0x0218, 'S' }, { 0x0219, 's' }, { 0x021A, 'T' }, { 0x021B, 't' }, { 0x021E, 'H' }, { 0x021F, 'h' }, { 0x0226, 'A' }, { 0x0227, 'a' }, { 0x0228, 'E' }, { 0x0229, 'e' }, { 0x022A, 'O' }, { 0x022B, 'o' }, { 0x022C, 'O' }, { 0x022D, 'o' }, { 0x022E, 'O' }, { 0x022F, 'o' }, { 0x0230, 'O' }, { 0x0231, 'o' }, { 0x0232, 'Y' }, { 0x0233, 'y' } };

int Code_Page_Byte(UINT page, char32_t code, bool & defaulted)
{
	defaulted = false;
	if (code < 0x80) return (int)code;
	char32_t const * table = page == 437 ? CodePage437High : (page == 1252 || page == 28591) ? CodePage1252High : nullptr;
	if (table == nullptr) return -1;
	for (int i = 0; i < 128; i++) {
		if (table[i] == code) return 0x80 + i;
	}
	for (BestFit const & fit : LatinBestFit) {
		if (fit.Code == code) return (unsigned char)fit.Base;
	}
	defaulted = true;
	return '?';
}

}


int WideCharToMultiByte(UINT codepage, DWORD, wchar_t const * wide, int widecount, char * multi, int multicount, char const *, BOOL * useddef)
{
	if (useddef != nullptr) *useddef = FALSE;
	if (wide == nullptr) return 0;
	bool const terminated = widecount < 0;
	if (terminated) widecount = (int)wcslen(wide) + 1;
	if (multicount == 0) multi = nullptr;

	if (codepage == 437 || codepage == 1252 || codepage == 28591) {
		if (multi != nullptr && multicount < widecount) {
			LastError = ERROR_INSUFFICIENT_BUFFER;
			return 0;
		}
		for (int i = 0; i < widecount; i++) {
			bool defaulted = false;
			int const byte = Code_Page_Byte(codepage, (char32_t)wide[i], defaulted);
			if (defaulted && useddef != nullptr) *useddef = TRUE;
			if (multi != nullptr) multi[i] = (char)byte;
		}
		return widecount;
	}

	int const result = Encode_UTF8(wide, widecount, multi, multicount);
	if (result == 0 && widecount > 0) LastError = ERROR_INSUFFICIENT_BUFFER;
	return result;
}


int MultiByteToWideChar(UINT, DWORD, char const * multi, int multicount, wchar_t * wide, int widecount)
{
	if (multi == nullptr) return 0;
	if (multicount < 0) multicount = (int)strlen(multi) + 1;
	if (widecount == 0) wide = nullptr;
	int const result = Decode_UTF8(multi, multicount, wide, widecount);
	if (result == 0 && multicount > 0) LastError = ERROR_INSUFFICIENT_BUFFER;
	return result;
}


wchar_t * CharUpperW(wchar_t * text)
{
	for (wchar_t * scan = text; scan != nullptr && *scan != L'\0'; scan++) {
		*scan = (wchar_t)towupper((wint_t)*scan);
	}
	return text;
}


/*
**	Memory. The block's size is kept in front of it so _aligned_realloc can copy it.
*/
namespace {
struct AlignedHeader
{
	void * Base;
	size_t Size;
};
}


void * _aligned_malloc(size_t size, size_t alignment)
{
	if (alignment < alignof(AlignedHeader)) alignment = alignof(AlignedHeader);
	size_t const total = size + alignment + sizeof(AlignedHeader);
	char * base = (char *)malloc(total);
	if (base == nullptr) return nullptr;
	uintptr_t const start = (uintptr_t)(base + sizeof(AlignedHeader));
	char * block = (char *)((start + alignment - 1) & ~(uintptr_t)(alignment - 1));
	AlignedHeader * header = (AlignedHeader *)(block - sizeof(AlignedHeader));
	header->Base = base;
	header->Size = size;
	return block;
}


void _aligned_free(void * block)
{
	if (block == nullptr) return;
	AlignedHeader * header = (AlignedHeader *)((char *)block - sizeof(AlignedHeader));
	free(header->Base);
}


void * _aligned_realloc(void * block, size_t size, size_t alignment)
{
	if (block == nullptr) return _aligned_malloc(size, alignment);
	if (size == 0) {
		_aligned_free(block);
		return nullptr;
	}
	AlignedHeader * header = (AlignedHeader *)((char *)block - sizeof(AlignedHeader));
	void * fresh = _aligned_malloc(size, alignment);
	if (fresh == nullptr) return nullptr;
	memcpy(fresh, block, std::min(size, header->Size));
	_aligned_free(block);
	return fresh;
}


void GlobalMemoryStatus(MEMORYSTATUS * status)
{
	if (status == nullptr) return;
	memset(status, 0, sizeof(*status));
	status->dwLength = sizeof(*status);
	unsigned long long total = 0;
	unsigned long long available = 0;
#if defined(__APPLE__)
	size_t length = sizeof(total);
	sysctlbyname("hw.memsize", &total, &length, nullptr, 0);
	available = total / 2;
#else
	struct sysinfo info;
	if (sysinfo(&info) == 0) {
		total = (unsigned long long)info.totalram * info.mem_unit;
		available = (unsigned long long)info.freeram * info.mem_unit;
	}
#endif
	status->dwTotalPhys = (SIZE_T)total;
	status->dwAvailPhys = (SIZE_T)available;
	status->dwTotalVirtual = (SIZE_T)total;
	status->dwAvailVirtual = (SIZE_T)available;
	status->dwMemoryLoad = total != 0 ? (DWORD)(100 - (available * 100) / total) : 0;
}


/*
**	Floating point. The engine asks for the default environment, which is what fesetenv
**	restores; the value reported back is always the default control word.
*/
unsigned int _controlfp(unsigned int value, unsigned int mask)
{
	if (mask != 0) {
		fesetenv(FE_DFL_ENV);
	}
	return _CW_DEFAULT;
}


int _controlfp_s(unsigned int * current, unsigned int value, unsigned int mask)
{
	unsigned int const result = _controlfp(value, mask);
	if (current != nullptr) *current = result;
	return 0;
}


/*
**	Windowing.
*/
int GetSystemMetrics(int index)
{
	switch (index) {
		case SM_CXDRAG:
		case SM_CYDRAG:			return 4;
		case SM_CXDOUBLECLK:
		case SM_CYDOUBLECLK:	return 4;
		default:				return 0;
	}
}
