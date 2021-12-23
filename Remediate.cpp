
#include "stdafx.h"
#include "Reports.h"
#include "Remediate.h"
#include "Utils.h"

#include <fstream>
#include <sstream>
#include <codecvt>
#include <regex>

const std::wregex line1_regex(L"Source: Manifest Vendor: ([^,]*), Manifest Version: ([^,]*), JNDI Class: ([^,]*), Log4j Vendor: ([^,]*), Log4j Version: ([^,]*)");
const std::wregex line2_regex(L"Path=(.*)");

bool ReadSignatureReport(const std::wstring& report, std::vector<CReportVulnerabilities>& result) {
  bool success{};
  DWORD file_size{};
  PBYTE buffer{};
  FILE* scan_file{};
  wchar_t error[1024]{};
  std::vector<std::wstring> lines;  

  std::wifstream wif(report);
  wif.imbue(std::locale(std::locale::empty(), new std::codecvt_utf8<wchar_t>));
  std::wstringstream wss;
  wss << wif.rdbuf();
  SplitWideString(wss.str(), L"\n", lines);

  for (uint32_t index = 0; index < lines.size(); index += SIGNATURE_ITEM_LENGTH) {
    std::wsmatch wsm1, wsm2;
    if (std::regex_match(lines[index].cbegin(), lines[index].cend(), wsm1, line1_regex)
      && std::regex_match(lines[index + 1].cbegin(), lines[index + 1].cend(), wsm2, line2_regex)) {

      std::wstring vendor = wsm1[1].str();
      std::wstring manifest_version = wsm1[2].str();
      bool jndi_class_found = (wsm1[3].str() == L"Found" ? true : false);
      std::wstring log4j_vendor = wsm1[4].str();
      std::wstring log4j_version = wsm1[5].str();
      std::wstring file = wsm2[1].str();
      
      result.emplace_back(file, manifest_version, vendor, false, false, false, jndi_class_found, false, log4j_version, log4j_vendor, false, false, false, false, L"");
    }
    else {
      swprintf_s(error, L"Failed to parse file %s", report.c_str());
      error_array.push_back(error);
      goto END;
    }
  }

  success = true;

END:

  if (wif.is_open()) {
    wif.close();
  }

  return success;
}

bool RemediateFile(const std::wstring& file) {
  bool success{};

RemediateLog4J::RemediateLog4J()
{
	fill_win32_filefunc64W(&ffunc_);
}

RemediateLog4J::~RemediateLog4J()
{
}

int RemediateLog4J::RemediateFileArchive(const std::wstring& vulnerable_file_path)
{
	std::vector<std::wstring> result;
	SplitWideString(vulnerable_file_path, L"!", result);

	// Copy original parent to temp parent1
	wchar_t	tmpPath[_MAX_PATH + 1]{};
	wchar_t tmpFilename[_MAX_PATH + 1]{};
	GetTempPath(_countof(tmpPath), tmpPath);
	GetTempFileName(tmpPath, L"qua", 0, tmpFilename);

	if (FALSE == CopyFile(result[0].c_str(), tmpFilename, FALSE))
	{
		return -1;
	}

	PairStack archives_mapping;

	archives_mapping.push(std::make_pair(result[0], tmpFilename));

	if (ExtractFileArchives(result, archives_mapping))
	{
		std::wcout << L"Failed to extract archives " << vulnerable_file_path << std::endl;
		return -1;
	}

	// 1. Pop the first jar and fix it. It is a vulnerable jar
	auto last_visited = archives_mapping.top();
	archives_mapping.pop();

	if (DeleteFileFromZIP(last_visited.second.c_str(), L"org/apache/logging/log4j/core/lookup/JndiLookup.class"))
	{
		std::wcout << L"Failed to delete vulnerable class from archive" << std::endl;
		return -1;
	}

	while (!archives_mapping.empty())
	{
		auto parent_jar_mapping = archives_mapping.top();
		archives_mapping.pop();

		if (ReplaceFileInZip(parent_jar_mapping.second, last_visited.first, last_visited.second))
		{
			std::wcout << L"Failed to repackage archive" << std::endl;
			return -1;
		}

		last_visited = parent_jar_mapping;
	}

	// 2 - 30 second

	// Make backup of original jar
	auto original_backup = result[0] + L".backup";
	if (_wrename(result[0].c_str(), original_backup.c_str()) != 0)
	{
		return -1;
	}

	// replace fixed jar with original
	if (_wrename(tmpFilename, result[0].c_str()) != 0)
	{
		return -1;
	}

	// delete the backup file

	// delete entry from log4j_findings.out

	// update remdiation report 

	Sleep(10000);

	return 0;
}

int RemediateLog4J::DeleteFileFromZIP(const std::wstring& zip_name, const std::wstring& del_file)
{
	return FixArchive(zip_name, del_file, L"", true);
}

int RemediateLog4J::ReplaceFileInZip(const std::wstring& target_zip_path, const std::wstring& vulnerable_zip_name, const std::wstring& fixed_zip_path)
{
	return FixArchive(target_zip_path, vulnerable_zip_name, fixed_zip_path, false);
}

int RemediateLog4J::FixArchive(const std::wstring& target_zip_path, const std::wstring& vulnerable_zip_name, const std::wstring& fixed_zip_path, bool delete_file)
{
	std::wstring temp_name = target_zip_path + L".tmp";

	zipFile szip = UnZipOpenFile(target_zip_path, &ffunc_);
	if (szip == NULL)
	{
		return -1;
	}

	zipFile dzip = ZipOpenFile(temp_name.c_str(), APPEND_STATUS_CREATE, NULL, &ffunc_);
	if (dzip == NULL)
	{
		unzClose(szip);
		return -1;
	}

	// get global commentary
	unz_global_info glob_info;
	if (unzGetGlobalInfo(szip, &glob_info) != UNZ_OK)
	{
		zipClose(dzip, NULL);
		unzClose(szip);
		return -1;
	}

	char* glob_comment = nullptr;
	if (glob_info.size_comment > 0)
	{
		//glob_comment = (char*)malloc(glob_info.size_comment + 1);
		glob_comment = new char[glob_info.size_comment + 1];

		if ((glob_comment == nullptr) && (glob_info.size_comment != 0))
		{
			zipClose(dzip, NULL);
			unzClose(szip);
			return -1;
		}

		SecureZeroMemory(glob_comment, glob_info.size_comment + 1);

		if ((unsigned int)unzGetGlobalComment(szip, glob_comment, glob_info.size_comment + 1) != glob_info.size_comment)
		{
			zipClose(dzip, NULL);
			unzClose(szip);
			SafeDeleteArray(glob_comment);
			return -1;
		}
	}

	int rv = unzGoToFirstFile(szip);
	while (rv == UNZ_OK)
	{
		// get zipped file info
		unz_file_info unzfi;
		char dos_fn[MAX_PATH];
		if (unzGetCurrentFileInfo(szip, &unzfi, dos_fn, MAX_PATH, NULL, 0, NULL, 0) != UNZ_OK)
		{
			break;
		}

		std::wstring file = A2W(dos_fn);

		OutputDebugString(file.c_str());
		OutputDebugString(L"\n");

		bool file_found = false;

		if (_wcsicmp(file.c_str(), vulnerable_zip_name.c_str()) == 0)
		{
			file_found = true;
		}

		// if not need delete this file
		if (file_found && delete_file) // lowercase comparison
		{
			rv = unzGoToNextFile(szip);
			continue;
		}
		else
		{
			char* extrafield = nullptr;
			char* commentary = nullptr;

			if (unzfi.size_file_extra > 0)
			{
				extrafield = new char[unzfi.size_file_extra];
			}
			if (unzfi.size_file_comment)
			{
				commentary = new char[unzfi.size_file_comment];
			}

			if (unzGetCurrentFileInfo(szip, &unzfi, dos_fn, MAX_PATH, extrafield, unzfi.size_file_extra, commentary, unzfi.size_file_comment) != UNZ_OK)
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				break;
			}

			// open file for RAW reading
			int method;
			int level;
			if (unzOpenCurrentFile2(szip, &method, &level, 1) != UNZ_OK)
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				break;
			}

			int size_local_extra = unzGetLocalExtrafield(szip, NULL, 0);
			if (size_local_extra < 0)
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				break;
			}

			void* local_extra = new BYTE[size_local_extra];
			if ((local_extra == NULL) && (size_local_extra != 0))
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				break;
			}

			if (unzGetLocalExtrafield(szip, local_extra, size_local_extra) < 0)
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				SafeDeleteArray(local_extra);
				break;
			}

			void* buf = nullptr;
			ULONG file_size = 0;
			// found the file that needs to be replaced
			if (file_found && !delete_file)
			{
				if (ReadFileContent(fixed_zip_path, &buf, &file_size))
				{
					SafeDeleteArray(extrafield);
					SafeDeleteArray(commentary);
					SafeDeleteArray(local_extra);
					break;
				}
			}
			else
			{
				// this malloc may fail if file very large
				buf = new BYTE[unzfi.compressed_size];
				if ((buf == NULL) && (unzfi.compressed_size != 0))
				{
					SafeDeleteArray(extrafield);
					SafeDeleteArray(commentary);
					SafeDeleteArray(local_extra);
					break;
				}

				// read file
				int sz = unzReadCurrentFile(szip, buf, unzfi.compressed_size);
				if ((unsigned int)sz != unzfi.compressed_size)
				{
					SafeDeleteArray(extrafield);
					SafeDeleteArray(commentary);
					SafeDeleteArray(local_extra);
					SafeDeleteArray(buf);
					break;
				}

				file_size = unzfi.compressed_size;
			}

			// open destination file
			zip_fileinfo zfi;
			memcpy(&zfi.tmz_date, &unzfi.tmu_date, sizeof(tm_unz));
			zfi.dosDate = unzfi.dosDate;
			zfi.internal_fa = unzfi.internal_fa;
			zfi.external_fa = unzfi.external_fa;

			if (zipOpenNewFileInZip2(dzip, dos_fn, &zfi, local_extra, size_local_extra, extrafield,
				unzfi.size_file_extra, commentary, method, level, (file_found && !delete_file) ? 0 : 1) != UNZ_OK)
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				SafeDeleteArray(local_extra);
				SafeDeleteArray(buf);
				break;
			}

			// write file
			if (zipWriteInFileInZip(dzip, buf, file_size) != UNZ_OK)
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				SafeDeleteArray(local_extra);
				SafeDeleteArray(buf);
				break;
			}

			if (zipCloseFileInZipRaw(dzip, (file_found && !delete_file) ? 0 : unzfi.uncompressed_size,
				(file_found && !delete_file) ? 0 : unzfi.crc) != UNZ_OK)
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				SafeDeleteArray(local_extra);
				SafeDeleteArray(buf);
				break;
			}

			if (unzCloseCurrentFile(szip) == UNZ_CRCERROR)
			{
				SafeDeleteArray(extrafield);
				SafeDeleteArray(commentary);
				SafeDeleteArray(local_extra);
				SafeDeleteArray(buf);
				break;
			}

			SafeDeleteArray(extrafield);
			SafeDeleteArray(commentary);
			SafeDeleteArray(local_extra);
			SafeDeleteArray(buf);
		}
		rv = unzGoToNextFile(szip);
	}

	zipClose(dzip, glob_comment);
	unzClose(szip);

	if (glob_comment)
	{
		SafeDeleteArray(glob_comment);
	}

	_wremove(target_zip_path.c_str());
	if (_wrename(temp_name.c_str(), target_zip_path.c_str()) != 0)
	{
		return -1;
	}
	return 0;
}

int RemediateLog4J::ReadFileContent(std::wstring file_path, void** buf, PULONG size)
{
	if (size == nullptr)
	{
		return 1;
	}

	// read file into buffer
	HANDLE handle_fixed_zip = CreateFile(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (handle_fixed_zip == INVALID_HANDLE_VALUE)
	{
		std::wcout << L"failed to open file for read " << file_path.c_str();
		return 1;
	}

	*size = GetFileSize(handle_fixed_zip, NULL);

	*buf = new BYTE[*size];
	if ((buf == NULL) && (*size != 0))
	{
		SAFE_CLOSE_HANDLE(handle_fixed_zip);
		return 1;
	}

	if (0 == ReadFile(handle_fixed_zip, *buf, *size, nullptr, nullptr))
	{
		SAFE_CLOSE_HANDLE(handle_fixed_zip);
		return 1;
	}

	SAFE_CLOSE_HANDLE(handle_fixed_zip);

	return 0;
}
  // Add fix logic here

  return success;
}

bool RemediateFromSignatureReport() {
  bool success{};  
  wchar_t error[1024]{};
  std::wstring signature_file;

  if (!ExpandEnvironmentVariables(qualys_program_data_location, signature_file)) {
    swprintf_s(error, L"Failed to expand path %s", qualys_program_data_location);
    error_array.push_back(error);
    goto END;
  }
    
  signature_file.append(L"\\").append(report_sig_output_file);

  if (!ReadSignatureReport(signature_file, vulnerabilities)) {
    swprintf_s(error, L"Failed to read signature file %s", signature_file.c_str());
    error_array.push_back(error);
    goto END;
  }  

  for (auto& vuln : repVulns) {
    
    // Add fix logic here

    // Remediation success
    if (true) {
      vuln.cve202144228Mitigated = true;
      vuln.cve202145046Mitigated = true;

      // Delete from signature file
    }
  }
  success = true;

END:

  return success;
}

bool DeleteVulnerabilityFromReport(const CReportVulnerabilities& vulnerability) {
  bool success{};
  wchar_t error[1024]{};
  std::wstring signature_file;
  std::vector<CReportVulnerabilities> report;

  if (!ExpandEnvironmentVariables(qualys_program_data_location, signature_file)) {
    swprintf_s(error, L"Failed to expand path %s", qualys_program_data_location);
    error_array.push_back(error);
    goto END;
  }

  signature_file.append(L"\\").append(report_sig_output_file);

  if (!ReadSignatureReport(signature_file, report)) {
    swprintf_s(error, L"Failed to read signature file %s", signature_file.c_str());
    error_array.push_back(error);
    goto END;
  }

  // Delete vulnerability if found in report

  success = true;
END:

  return success;
}
