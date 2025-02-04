#pragma once


int32_t ScanFileArchive(bool console, bool verbose, std::wstring file, std::wstring alternate);
int32_t ScanFile(bool console, bool verbose, std::wstring file);
int32_t ScanDirectory(bool console, bool verbose, std::wstring directory);
int32_t ScanLocalDrives(bool console, bool verbose);
int32_t ScanNetworkDrives(bool console, bool verbose);
int32_t ScanLocalDrivesInclMountpoints(bool console, bool verbose);
int32_t EnumMountPoints(bool console, bool verbose, LPCWSTR szVolume);
