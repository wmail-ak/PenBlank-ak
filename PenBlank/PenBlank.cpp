
//Copyright Arsenii K. decode@tutanota.de, 2026
// 


#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <conio.h>
#include <filesystem>
#include <cmath>


constexpr uint32_t ONE_GIBIBYTE_BYTES = 1073741824;
constexpr UINT32 ONE_MIBIBYTE_BYTES = 1048576;


static void PrintSentence(const std::wstring& s) {
	std::wcout << s << std::endl;
}

static void PrintError(const std::wstring& s) {
	std::wcerr << s << std::endl;
}

static bool SetDiskOffline(const std::wstring& devicePath, bool offline) {
	HANDLE hDisk = CreateFileW(devicePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
		NULL);

	if (hDisk == INVALID_HANDLE_VALUE) {
		PrintError(std::format( L"Failed to open device: {}  Error: {}", devicePath, GetLastError()));
		return false;
	}

	SET_DISK_ATTRIBUTES attrs{};
	attrs.Version = sizeof(SET_DISK_ATTRIBUTES);
	attrs.Persist = TRUE;
	attrs.Attributes = offline ? 1 : 0;   // mark disk offline
	attrs.AttributesMask = DISK_ATTRIBUTE_OFFLINE;

	DWORD bytesReturned;
	BOOL ok = DeviceIoControl(hDisk,
		IOCTL_DISK_SET_DISK_ATTRIBUTES,
		&attrs,
		sizeof(attrs),
		NULL,
		0,
		&bytesReturned,
		NULL);

	CloseHandle(hDisk);

	if (!ok) {
		PrintError(std::format(L"DeviceIoControl failed. Error: {}",  GetLastError()));
		return false;
	}
	PrintSentence(std::format(L"Disk {} set {} successfully.", devicePath, (offline ? "offline" : "online")));
	return true;
}

static bool SetDiskReadonly(const std::wstring& devicePath, bool readonly) {
	HANDLE hDisk = CreateFileW(devicePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
		NULL);

	if (hDisk == INVALID_HANDLE_VALUE) {
		PrintError(std::format(L"Failed to open device: {}  Error: {}", devicePath, GetLastError()));
		return false;
	}

	SET_DISK_ATTRIBUTES attrs{};
	attrs.Version = sizeof(SET_DISK_ATTRIBUTES);
	attrs.Persist = FALSE; 
	attrs.Attributes = readonly ? 1 : 0;
	attrs.AttributesMask = DISK_ATTRIBUTE_READ_ONLY;


	DWORD bytesReturned;
	BOOL ok = DeviceIoControl(hDisk,
		IOCTL_DISK_SET_DISK_ATTRIBUTES,
		&attrs,
		sizeof(attrs),
		NULL,
		0,
		&bytesReturned,
		NULL);

	CloseHandle(hDisk);

	if (!ok) {
		PrintError(std::format(L"DeviceIoControl failed. Error: {}", GetLastError()));
		return false;
	}
	PrintSentence(std::format(L"Disk {} set {} successfully.", devicePath, (readonly ? "+readonly" : "-readonly")));
	return true;
}

static void listDrives() {
	for (uint8_t i = 0; i < 64; ++i) {
		std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);
		HANDLE hDrive = CreateFileW(path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hDrive == INVALID_HANDLE_VALUE) continue;

		DISK_GEOMETRY_EX geom{};
		DWORD bytes;
		if (DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			NULL, 0, &geom, sizeof(geom), &bytes, NULL)) {
			PrintSentence(std::format(L"{}: {} Size: {} GiB", i, path, static_cast<long long>(geom.DiskSize.QuadPart / ONE_GIBIBYTE_BYTES)));
		}
		CloseHandle(hDrive);
	}
}


static bool confirmDestructive() {
	PrintSentence(L"WARNING: This will erase the drive! Press any key to Continue.");
	for (WORD i = 700; i > 0; --i) {
		PrintSentence(std::format(L"Auto cancellation in {} seconds...    ", i));
		Sleep(1000);
		if (_kbhit()) {
			wchar_t x = _getch();
			PrintSentence(std::format(L"Continue. Key pressed: {}", (std::isprint(x) ? std::wstring(1, x) : L"non-printable(" + std::to_wstring(x) + L")")));
			return true;
		}
	}
	PrintSentence(L"Canceled");
	return false;
}

static bool confirmDestructivePath(const std::wstring& isoPath) {
	PrintSentence(std::format(L"WARNING: Handle for {} drive was opened!", isoPath));
	for (uint16_t i = 700; i > 0; --i) {
		PrintSentence(std::format(L"Auto cancellation in {} seconds...    ", i));
		Sleep(1000);
		if (_kbhit()) {
			wchar_t x = _getch();
			PrintSentence(std::format(L"Continue. Key pressed: {}", (std::isprint(x) ? std::wstring(1, x) : L"non-printable(" + std::to_wstring(x) + L")")));
			return true;
		}
	}
	PrintSentence(L"Canceled");
	return false;
}

static bool writeISO(std::ifstream& iso_sm, size_t ISOsize, const std::wstring& drivePath) {
	//	return true;
	size_t total = 0;

	HANDLE hDrive = CreateFileW(drivePath.c_str(), GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);

	if (hDrive == INVALID_HANDLE_VALUE) {
		PrintError(std::format(L"Error opening drive. {}", drivePath));
		return false;
	}

	if (confirmDestructivePath(drivePath)) {
		PrintSentence(L"Written: 0 MiB, [          ] 0%");

		const DWORD BUF_SIZE = 1 << 20; // 128 MiB
		std::vector<char> buffer(BUF_SIZE);
		LARGE_INTEGER pos{};
		DWORD written = 0;

		auto start = GetTickCount64();
		while (true) {
			iso_sm.read(buffer.data(), BUF_SIZE);
			std::streamsize bytesRead = iso_sm.gcount();
			if (bytesRead <= 0) break;

			if (!WriteFile(hDrive, buffer.data(), (DWORD)iso_sm.gcount(), &written, NULL))break;
			if (written == 0) break;

			total += written;

			// log every second
			if (((GetTickCount64() - start) >= 100) || (total == ISOsize)) {
				uint8_t percent = (uint8_t)(100 * total / ISOsize);
				uint8_t blocks = percent / 10; // how many '=' out of 10
				uint8_t spaces = 10 - blocks;

				PrintSentence(std::format(L"Written: {} MiB, [{}] {}%", static_cast<long long>(total / ONE_MIBIBYTE_BYTES), std::wstring(blocks, '=') + std::wstring(spaces, ' '), percent));
				start = GetTickCount64();
			}
		}
	}
	FlushFileBuffers(hDrive);
	CloseHandle(hDrive);

	PrintSentence(L"Write complete.");
	PrintSentence(std::format(L"Total.{}", ISOsize));
	PrintSentence(std::format(L"Written.{}", total));

	return  (total == ISOsize) ? true : false;
}

static void compareISOWithDrive(std::ifstream& iso_sm, size_t ISOsize, const std::wstring& drivePath) {

	iso_sm.clear();
	iso_sm.seekg(0, std::ios::beg);


	HANDLE hDrive = CreateFileW(drivePath.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);

	if (hDrive == INVALID_HANDLE_VALUE) {
		PrintError(std::format(L"Error opening drive. {}", drivePath));
		return;
	}

	const DWORD BUF_SIZE = 1 << 20; // 128 MiB
	std::vector<char> bufIso(BUF_SIZE), bufUsb(BUF_SIZE);
	DWORD readUsb = 0;
	size_t total = 0;

	auto start = GetTickCount64();
	while (true) {
		iso_sm.read(bufIso.data(), BUF_SIZE);
		std::streamsize bytesRead = iso_sm.gcount();
		if (bytesRead <= 0) break;

		if (!ReadFile(hDrive, bufUsb.data(), (DWORD)bytesRead, &readUsb, NULL) || readUsb != bytesRead) {
			PrintError(L"Mismatch: unable to read same amount from USB.");
			break;
		}

		if (memcmp(bufIso.data(), bufUsb.data(), readUsb) != 0) {
			PrintError(std::format(L"Mismatch detected at offset {} bytes.", total));
			break;
		}

		total += readUsb;

		if (((GetTickCount64() - start) >= 100) || (total == ISOsize)) {
			uint8_t percent = (uint8_t)(100 * total / ISOsize);
			uint8_t blocks = percent / 10; // how many '=' out of 10
			uint8_t spaces = 10 - blocks;

			PrintSentence(std::format(L"Compared: {} MiB, [{}] {}%", static_cast<long long>(total / ONE_MIBIBYTE_BYTES), std::wstring(blocks, '=') + std::wstring(spaces, ' '), percent));
			start = GetTickCount64();
		}
	}

	FlushFileBuffers(hDrive);
	CloseHandle(hDrive);
	PrintSentence(std::format(L"Comparison complete, compared.{}", total));
}




int main() {
	std::wstring isoPath;
	PrintSentence(L"Enter path to ISO:");
	std::getline(std::wcin, isoPath);

	try {

		std::ifstream iso_sm(isoPath, std::ios::binary);

		if (iso_sm && iso_sm.is_open()) {
			size_t ISOsize = std::filesystem::file_size(isoPath);
			listDrives();
			PrintSentence(L"Select drive number: ");
			
			uint8_t driveNum;
			std::cin >> driveNum;

			std::wstring drivePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(driveNum);

			if (SetDiskOffline(drivePath, true) && SetDiskReadonly(drivePath, false)) {

				if (!confirmDestructive()) {
					PrintSentence(L"Operation cancelled.");
					return 0;
				}

				if (writeISO(iso_sm, ISOsize, drivePath)) {
					//SetDiskReadonly(drivePath, true);
					//SetDiskOffline(drivePath, false);

					// Safe eject warning
					PrintSentence(L"IMPORTANT: Safely eject and reinsert the USB drive to ensure caches are flushed.");
					PrintSentence(L"Press any key to SKIP waiting, otherwise comparison will start in 700 seconds...");

					for (uint16_t i = 700; i > 0; --i) {
						PrintSentence(std::format(L"Starting comparison in {} seconds...    ", i));
						Sleep(1000);
						if (_kbhit()) {
							wchar_t x = _getch();
							PrintSentence(std::format(L"Continue. Key pressed: {}", (std::isprint(x) ? std::wstring(1, x) : L"non-printable(" + std::to_wstring(x) + L")")));
							PrintSentence(std::format(L"User skipped wait."));
							break;
						}
					}
					PrintSentence(std::format(L"Continue."));
					compareISOWithDrive(iso_sm, ISOsize, drivePath);

				}
				else {
					PrintError(L"Something gone wronge at writeISO task");
				}
				SetDiskOffline(drivePath, false);
			}
		}
		else {
			PrintError(L"Error opening ISO.");
		}
	}
	catch (...) {
		PrintError(L"Something unpredictable happened");
	}

	PrintSentence(L"Press any key to exit: ");
	wchar_t x = _getch();
	PrintSentence(std::format(L"Continue. Key pressed: {}", (std::isprint(x) ? std::wstring(1, x) : L"non-printable(" + std::to_wstring(x) + L")")));

	return 0;
}
