#include <windows.h>
#include <wincrypt.h>
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#ifndef CALG_SHA_256
#define CALG_SHA_256 (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_SHA_256)
#define ALG_SID_SHA_256 12
#endif

#pragma comment(lib, "advapi32.lib")

using namespace std;

// Statistics structure
struct BackupStats {
    int filesProcessed = 0;
    int filesSkipped = 0;
    int filesCopied = 0;
    int filesNew = 0;
    int filesModified = 0;
    int directoriesCreated = 0;
    int errors = 0;
    long long totalBytes = 0;
    long long bytesCopied = 0;
};

// File metadata structure
struct FileMetadata {
    string hash;
    long long size;
    time_t lastModified;
};

// SHA-256 Hasher Class
class FileHasher {
public:
    // Calculate SHA-256 hash of a file
    static string CalculateHash(const string& filePath) {
        HANDLE hFile = CreateFileA(
            filePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN,
            NULL
        );

        if (hFile == INVALID_HANDLE_VALUE) {
            return "";
        }

        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        
        // Acquire crypto context
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            CloseHandle(hFile);
            return "";
        }

        // Create hash object
        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return "";
        }

        // Read file and hash in chunks
        const DWORD BUFFER_SIZE = 8192; // 8KB chunks
        BYTE buffer[BUFFER_SIZE];
        DWORD bytesRead = 0;

        while (ReadFile(hFile, buffer, BUFFER_SIZE, &bytesRead, NULL) && bytesRead > 0) {
            if (!CryptHashData(hHash, buffer, bytesRead, 0)) {
                CryptDestroyHash(hHash);
                CryptReleaseContext(hProv, 0);
                CloseHandle(hFile);
                return "";
            }
        }

        // Get hash result
        BYTE hashResult[32]; // SHA-256 produces 32 bytes
        DWORD hashLen = 32;
        
        if (!CryptGetHashParam(hHash, HP_HASHVAL, hashResult, &hashLen, 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return "";
        }

        // Convert to hex string
        stringstream ss;
        for (DWORD i = 0; i < hashLen; i++) {
            ss << hex << setw(2) << setfill('0') << (int)hashResult[i];
        }

        // Cleanup
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);

        return ss.str();
    }
};

// Manifest Manager Class
class ManifestManager {
private:
    map<string, FileMetadata> manifest;
    string manifestPath;

public:
    ManifestManager(const string& backupRoot) {
        manifestPath = backupRoot + "\\.backup_manifest.txt";
        cout << "Saving manifest at: " << manifestPath << endl;
    }

    // Load manifest from file
    bool Load() {
        ifstream file(manifestPath);
        if (!file.is_open()) {
            // Manifest doesn't exist - this is first backup
            return false;
        }

        manifest.clear();
        string line;
        
        while (getline(file, line)) {
            if (line.empty()) continue;

            // Parse line: filepath|hash|size|timestamp
            size_t pos1 = line.find('|');
            size_t pos2 = line.find('|', pos1 + 1);
            size_t pos3 = line.find('|', pos2 + 1);

            if (pos1 != string::npos && pos2 != string::npos && pos3 != string::npos) {
                string filepath = line.substr(0, pos1);
                string hash = line.substr(pos1 + 1, pos2 - pos1 - 1);
                long long size = stoll(line.substr(pos2 + 1, pos3 - pos2 - 1));
                time_t timestamp = stoll(line.substr(pos3 + 1));

                FileMetadata meta;
                meta.hash = hash;
                meta.size = size;
                meta.lastModified = timestamp;
                manifest[filepath] = meta;
            }
        }

        file.close();
        return true;
    }

    // Save manifest to file
    bool Save() {
        ofstream file(manifestPath);
        if (!file.is_open()) {
            return false;
        }

        for (const auto& entry : manifest) {
            file << entry.first << "|"
                 << entry.second.hash << "|"
                 << entry.second.size << "|"
                 << entry.second.lastModified << "\n";
        }

        file.close();
        return true;
    }

    // Check if file exists in manifest
    bool HasFile(const string& filepath) {
        return manifest.find(filepath) != manifest.end();
    }

    // Get file metadata from manifest
    FileMetadata GetFileMetadata(const string& filepath) {
        return manifest[filepath];
    }

    // Update/Add file in manifest
    void UpdateFile(const string& filepath, const FileMetadata& meta) {
        manifest[filepath] = meta;
    }

    // Get manifest size
    int GetFileCount() {
        return manifest.size();
    }
};

// Main Backup Class
class IncrementalBackup {
private:
    string sourcePath;
    string destPath;
    BackupStats stats;
    ManifestManager manifest;
    bool incrementalMode;

    string NormalizePath(const string& path) {
        string normalized = path;
        if (!normalized.empty() && normalized.back() != '\\') {
            normalized += '\\';
        }
        return normalized;
    }

    string GetRelativePath(const string& fullPath, const string& basePath) {
        if (fullPath.find(basePath) == 0) {
            return fullPath.substr(basePath.length());
        }
        return fullPath;
    }

    bool CreateDestDirectory(const string& path) {
        DWORD attribs = GetFileAttributesA(path.c_str());
        if (attribs != INVALID_FILE_ATTRIBUTES && 
            (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
            return true;
        }

        if (CreateDirectoryA(path.c_str(), NULL)) {
            stats.directoriesCreated++;
            return true;
        }
        
        DWORD error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) {
            return true;
        }
        
        if (error == ERROR_PATH_NOT_FOUND) {
            size_t pos = path.find_last_of("\\/", path.length() - 2);
            if (pos != string::npos) {
                string parentPath = path.substr(0, pos);
                if (CreateDestDirectory(parentPath)) {
                    return CreateDestDirectory(path);
                }
            }
        }
        
        return false;
    }

    long long GetFileSize(WIN32_FIND_DATAA& findData) {
        LARGE_INTEGER size;
        size.LowPart = findData.nFileSizeLow;
        size.HighPart = findData.nFileSizeHigh;
        return size.QuadPart;
    }

    time_t GetFileTime(WIN32_FIND_DATAA& findData) {
        ULARGE_INTEGER ull;
        ull.LowPart = findData.ftLastWriteTime.dwLowDateTime;
        ull.HighPart = findData.ftLastWriteTime.dwHighDateTime;
        return ull.QuadPart / 10000000ULL - 11644473600ULL;
    }

    bool ShouldCopyFile(const string& sourceFile, const string& relativePath, 
                       long long fileSize, time_t fileTime, string& currentHash) {
        
        // If not in incremental mode, copy everything
        if (!incrementalMode) {
            currentHash = FileHasher::CalculateHash(sourceFile);
            return true;
        }

        // Check if file exists in manifest
        if (!manifest.HasFile(relativePath)) {
            // New file - must copy
            cout << "  [NEW] ";
            currentHash = FileHasher::CalculateHash(sourceFile);
            stats.filesNew++;
            return true;
        }

        // File exists in manifest - check if changed
        FileMetadata oldMeta = manifest.GetFileMetadata(relativePath);

        // Quick check: if size or time different, likely changed
        if (oldMeta.size != fileSize || oldMeta.lastModified != fileTime) {
            // Calculate hash to confirm
            currentHash = FileHasher::CalculateHash(sourceFile);
            
            if (currentHash != oldMeta.hash) {
                // File actually changed
                cout << "  [MODIFIED] ";
                stats.filesModified++;
                return true;
            }
        } else {
            // Size and time same - assume unchanged (optimization)
            currentHash = oldMeta.hash;
        }

        // File unchanged - skip
        cout << "  [SKIP] ";
        stats.filesSkipped++;
        return false;
    }

    bool BackupDirectory(const string& sourceDir, const string& destDir) {
        string searchPath = sourceDir + "*";
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
        
        if (hFind == INVALID_HANDLE_VALUE) {
            cerr << "ERROR: Cannot access directory: " << sourceDir << endl;
            stats.errors++;
            return false;
        }

        if (!CreateDestDirectory(destDir)) {
            cerr << "ERROR: Cannot create directory: " << destDir << endl;
            stats.errors++;
            FindClose(hFind);
            return false;
        }

        do {
            string fileName = findData.cFileName;
            
            if (fileName == "." || fileName == "..") {
                continue;
            }

            string sourceFullPath = sourceDir + fileName;
            string destFullPath = destDir + fileName;
            string relativePath = GetRelativePath(sourceFullPath, sourcePath);
            
            stats.filesProcessed++;

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                cout << "\nEntering directory: " << sourceFullPath << endl;
                BackupDirectory(sourceFullPath + "\\", destFullPath + "\\");
            } else {
                long long fileSize = GetFileSize(findData);
                time_t fileTime = GetFileTime(findData);
                stats.totalBytes += fileSize;

                string fileHash;
                if (ShouldCopyFile(sourceFullPath, relativePath, fileSize, fileTime, fileHash)) {
                    cout << sourceFullPath << endl;
                    
                    if (CopyFileA(sourceFullPath.c_str(), destFullPath.c_str(), FALSE)) {
                        stats.filesCopied++;
                        stats.bytesCopied += fileSize;

                        // Update manifest
                        FileMetadata meta;
                        meta.hash = fileHash;
                        meta.size = fileSize;
                        meta.lastModified = fileTime;
                        manifest.UpdateFile(relativePath, meta);
                    } else {
                        cerr << "  ERROR: Failed to copy file" << endl;
                        stats.errors++;
                    }
                } else {
                    cout << sourceFullPath << endl;
                    
                    // File skipped but update manifest (in case metadata changed)
                    FileMetadata meta;
                    meta.hash = fileHash;
                    meta.size = fileSize;
                    meta.lastModified = fileTime;
                    manifest.UpdateFile(relativePath, meta);
                }
            }
            
        } while (FindNextFileA(hFind, &findData));

        FindClose(hFind);
        return true;
    }

public:
    IncrementalBackup(const string& src, const string& dst, bool incremental = true) 
        : manifest(dst), incrementalMode(incremental) {
        sourcePath = NormalizePath(src);
        destPath = NormalizePath(dst);
    }

    bool StartBackup() {
        cout << "========================================" << endl;
        cout << "  FILE BACKUP TOOL - Phase 2" << endl;
        cout << "========================================" << endl;
        cout << "Source: " << sourcePath << endl;
        cout << "Destination: " << destPath << endl;
        
        // Load previous manifest
        bool hasManifest = manifest.Load();
        if (hasManifest && incrementalMode) {
            cout << "Mode: INCREMENTAL (found " << manifest.GetFileCount() 
                 << " files in previous backup)" << endl;
        } else {
            cout << "Mode: FULL (no previous backup found)" << endl;
            incrementalMode = false;
        }
        
        cout << "========================================\n" << endl;

        // Verify source exists
        DWORD attribs = GetFileAttributesA(sourcePath.c_str());
        if (attribs == INVALID_FILE_ATTRIBUTES) {
            cerr << "ERROR: Source directory does not exist!" << endl;
            return false;
        }
        if (!(attribs & FILE_ATTRIBUTE_DIRECTORY)) {
            cerr << "ERROR: Source path is not a directory!" << endl;
            return false;
        }

        // Start backup
        bool result = BackupDirectory(sourcePath, destPath);
        
        // Save updated manifest
        if (!manifest.Save()) {
            cerr << "WARNING: Failed to save manifest file" << endl;
        }

        // Print statistics
        PrintStats();
        
        return result;
    }

    void PrintStats() {
        cout << "\n========================================" << endl;
        cout << "  BACKUP COMPLETE" << endl;
        cout << "========================================" << endl;
        cout << "Files processed:      " << stats.filesProcessed << endl;
        cout << "Files copied:         " << stats.filesCopied << endl;
        
        if (incrementalMode || stats.filesNew > 0 || stats.filesModified > 0) {
            cout << "  - New files:        " << stats.filesNew << endl;
            cout << "  - Modified files:   " << stats.filesModified << endl;
            cout << "Files skipped:        " << stats.filesSkipped << endl;
        }
        
        cout << "Directories created:  " << stats.directoriesCreated << endl;
        cout << "Errors:               " << stats.errors << endl;
        cout << "Total size:           " << FormatBytes(stats.totalBytes) << endl;
        cout << "Bytes copied:         " << FormatBytes(stats.bytesCopied) << endl;
        
        if (stats.totalBytes > 0) {
            double savedPercent = ((stats.totalBytes - stats.bytesCopied) * 100.0) / stats.totalBytes;
            cout << "Space saved:          " << FormatBytes(stats.totalBytes - stats.bytesCopied)
                 << " (" << fixed << setprecision(1) << savedPercent << "%)" << endl;
        }
        
        cout << "========================================" << endl;
    }

    string FormatBytes(long long bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unitIndex = 0;
        double size = (double)bytes;
        
        while (size >= 1024 && unitIndex < 4) {
            size /= 1024;
            unitIndex++;
        }
        
        char buffer[32];
        snprintf(buffer, 32, "%.2f %s", size, units[unitIndex]);
        return string(buffer);
    }
};

int main(int argc, char* argv[]) {
    string source, dest;
    bool incremental = true;
    
    if (argc >= 3) {
        source = argv[1];
        dest = argv[2];
        
        // Check for --full flag
        for (int i = 3; i < argc; i++) {
            string arg = argv[i];
            if (arg == "--full" || arg == "-f") {
                incremental = false;
                cout << "Full backup mode enabled.\n" << endl;
            }
        }
    } else {
        cout << "Enter source directory path: ";
        getline(cin, source);
        
        cout << "Enter destination directory path: ";
        getline(cin, dest);
        
        cout << "Incremental backup? (y/n): ";
        string choice;
        getline(cin, choice);
        incremental = (choice == "y" || choice == "Y" || choice == "yes");
    }

    if (source.empty() || dest.empty()) {
        cerr << "ERROR: Source and destination paths are required!" << endl;
        cout << "\nUsage: backup.exe <source_path> <dest_path> [--full]" << endl;
        cout << "Example: backup.exe C:\\MyDocuments D:\\Backup" << endl;
        cout << "         backup.exe C:\\MyDocuments D:\\Backup --full" << endl;
        return 1;
    }

    IncrementalBackup backup(source, dest, incremental);
    bool success = backup.StartBackup();
    
    if (success) {
        cout << "\nBackup completed successfully!" << endl;
        return 0;
    } else {
        cout << "\nBackup completed with errors!" << endl;
        return 1;
    }
}