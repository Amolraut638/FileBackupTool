#include <windows.h>
#include <wincrypt.h>
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

#pragma comment(lib, "advapi32.lib")

#ifndef CALG_SHA_256
#define CALG_SHA_256 (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_SHA_256)
#define ALG_SID_SHA_256 12
#endif

using namespace std;

// Statistics structure
struct BackupStats {
    int filesProcessed = 0;
    int filesSkipped = 0;
    int filesCopied = 0;
    int filesNew = 0;
    int filesModified = 0;
    int filesDeduped = 0;  // Files that shared existing content
    int directoriesCreated = 0;
    int errors = 0;
    long long totalBytes = 0;
    long long bytesCopied = 0;
    long long bytesDeduplicated = 0;  // Space saved by deduplication
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
        
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            CloseHandle(hFile);
            return "";
        }

        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return "";
        }

        const DWORD BUFFER_SIZE = 8192;
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

        BYTE hashResult[32];
        DWORD hashLen = 32;
        
        if (!CryptGetHashParam(hHash, HP_HASHVAL, hashResult, &hashLen, 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return "";
        }

        stringstream ss;
        for (DWORD i = 0; i < hashLen; i++) {
            ss << hex << setw(2) << setfill('0') << (int)hashResult[i];
        }

        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);

        return ss.str();
    }
};

// Deduplication Store Class
class DeduplicationStore {
private:
    string storePath;  // Path to .dedup_store folder
    map<string, int> referenceCount;  // Track how many files point to each hash

public:
    DeduplicationStore(const string& backupRoot) {
        // Ensure backupRoot ends with backslash
        string root = backupRoot;
        if (!root.empty() && root.back() != '\\') {
            root += '\\';
        }
        storePath = root + ".dedup_store\\";
    }

    // Initialize store - create .dedup_store folder if needed
    bool Initialize() {
        // First, check if parent directory exists
        string parentPath = storePath.substr(0, storePath.length() - 14); // Remove ".dedup_store\"
        
        DWORD parentAttribs = GetFileAttributesA(parentPath.c_str());
        if (parentAttribs == INVALID_FILE_ATTRIBUTES) {
            // Parent directory doesn't exist - create it
            if (!CreateDirectoryA(parentPath.c_str(), NULL)) {
                DWORD error = GetLastError();
                if (error != ERROR_ALREADY_EXISTS) {
                    cerr << "ERROR: Cannot create parent directory: " << parentPath << endl;
                    return false;
                }
            }
        }
        
        // Check if .dedup_store already exists
        DWORD attribs = GetFileAttributesA(storePath.c_str());
        if (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
            return true;  // Already exists
        }

        // Create .dedup_store directory
        if (!CreateDirectoryA(storePath.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_ALREADY_EXISTS) {
                return true;  // That's okay
            }
            cerr << "ERROR: Cannot create dedup store: " << storePath << " (Error: " << error << ")" << endl;
            return false;
        }

        return true;
    }

    // Get path for storing content by hash
    string GetContentPath(const string& hash) {
        return storePath + hash + ".bin";
    }

    // Check if content already exists
    bool ContentExists(const string& hash) {
        string contentPath = GetContentPath(hash);
        DWORD attribs = GetFileAttributesA(contentPath.c_str());
        return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
    }

    // Store file content by hash (copy file to .dedup_store)
    bool StoreContent(const string& sourceFile, const string& hash) {
        string destPath = GetContentPath(hash);

        if (CopyFileA(sourceFile.c_str(), destPath.c_str(), FALSE)) {
            referenceCount[hash] = 1;
            return true;
        }

        return false;
    }

    // Increment reference count (file points to this hash)
    void IncrementReference(const string& hash) {
        referenceCount[hash]++;
    }

    // Get reference count for a hash
    int GetReferenceCount(const string& hash) {
        auto it = referenceCount.find(hash);
        if (it != referenceCount.end()) {
            return it->second;
        }
        return 0;
    }

    // Load reference counts from store
    void LoadReferenceCountsFromIndex(const map<string, string>& fileHashMap) {
        referenceCount.clear();
        for (const auto& entry : fileHashMap) {
            referenceCount[entry.second]++;
        }
    }

    // Get store path for public use
    string GetStorePath() {
        return storePath;
    }
};

// Deduplication Index Class
class DeduplicationIndex {
private:
    map<string, string> fileHashMap;  // filepath → hash
    string indexPath;

public:
    DeduplicationIndex(const string& backupRoot) {
        // Ensure backupRoot ends with backslash
        string root = backupRoot;
        if (!root.empty() && root.back() != '\\') {
            root += '\\';
        }
        indexPath = root + ".dedup_index.txt";
    }

    // Load index from file
    bool Load() {
        ifstream file(indexPath);
        if (!file.is_open()) {
            return false;
        }

        fileHashMap.clear();
        string line;

        while (getline(file, line)) {
            if (line.empty()) continue;

            size_t pos = line.find('|');
            if (pos != string::npos) {
                string filepath = line.substr(0, pos);
                string hash = line.substr(pos + 1);
                fileHashMap[filepath] = hash;
            }
        }

        file.close();
        return true;
    }

    // Save index to file
    bool Save() {
        ofstream file(indexPath);
        if (!file.is_open()) {
            return false;
        }

        for (const auto& entry : fileHashMap) {
            file << entry.first << "|" << entry.second << "\n";
        }

        file.close();
        return true;
    }

    // Add file to index
    void AddFile(const string& filepath, const string& hash) {
        fileHashMap[filepath] = hash;
    }

    // Get hash for file
    string GetHash(const string& filepath) {
        auto it = fileHashMap.find(filepath);
        if (it != fileHashMap.end()) {
            return it->second;
        }
        return "";
    }

    // Check if file exists in index
    bool HasFile(const string& filepath) {
        return fileHashMap.find(filepath) != fileHashMap.end();
    }

    // Get all files (for loading reference counts)
    const map<string, string>& GetAllFiles() {
        return fileHashMap;
    }

    // Get file count
    int GetFileCount() {
        return fileHashMap.size();
    }
};

// Main Deduplication Backup Class
class DeduplicationBackup {
private:
    string sourcePath;
    string destPath;
    BackupStats stats;
    DeduplicationStore store;
    DeduplicationIndex index;

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
                stats.totalBytes += fileSize;

                // Calculate hash
                string fileHash = FileHasher::CalculateHash(sourceFullPath);
                if (fileHash.empty()) {
                    cerr << "  ERROR: Failed to calculate hash" << endl;
                    stats.errors++;
                    continue;
                }

                // Check if content already exists in store
                if (store.ContentExists(fileHash)) {
                    // Content already stored - just reference it
                    cout << "  [DEDUP] " << sourceFullPath << " (already stored)" << endl;
                    stats.filesDeduped++;
                    stats.bytesDeduplicated += fileSize;
                    store.IncrementReference(fileHash);
                } else {
                    // New content - store it
                    cout << "  [NEW] " << sourceFullPath << endl;
                    if (store.StoreContent(sourceFullPath, fileHash)) {
                        stats.filesCopied++;
                        stats.bytesCopied += fileSize;
                    } else {
                        cerr << "  ERROR: Failed to store content" << endl;
                        stats.errors++;
                        continue;
                    }
                }

                // Add to index
                index.AddFile(relativePath, fileHash);
            }
            
        } while (FindNextFileA(hFind, &findData));

        FindClose(hFind);
        return true;
    }

public:
    DeduplicationBackup(const string& src, const string& dst)
        : store(dst), index(dst) {
        sourcePath = NormalizePath(src);
        destPath = NormalizePath(dst);
    }

    bool StartBackup() {
        cout << "========================================" << endl;
        cout << "  FILE BACKUP TOOL - Phase 3" << endl;
        cout << "  Deduplication Enabled" << endl;
        cout << "========================================" << endl;
        cout << "Source: " << sourcePath << endl;
        cout << "Destination: " << destPath << endl;
        cout << "========================================\n" << endl;

        // Initialize deduplication store
        if (!store.Initialize()) {
            cerr << "ERROR: Failed to initialize deduplication store" << endl;
            return false;
        }

        // Load existing index
        bool hasIndex = index.Load();
        if (hasIndex) {
            store.LoadReferenceCountsFromIndex(index.GetAllFiles());
            cout << "Loaded existing index with " << index.GetFileCount() << " files" << endl;
        }

        cout << "Dedup store: " << store.GetStorePath() << "\n" << endl;

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
        
        // Save updated index
        if (!index.Save()) {
            cerr << "WARNING: Failed to save index file" << endl;
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
        cout << "Files copied:         " << stats.filesCopied << " (new content)" << endl;
        cout << "Files deduplicated:   " << stats.filesDeduped << " (shared content)" << endl;
        cout << "Directories created:  " << stats.directoriesCreated << endl;
        cout << "Errors:               " << stats.errors << endl;
        
        cout << "\nStorage Analysis:" << endl;
        cout << "Total source size:    " << FormatBytes(stats.totalBytes) << endl;
        cout << "Actual data stored:   " << FormatBytes(stats.bytesCopied) << endl;
        cout << "Space saved (dedup):  " << FormatBytes(stats.bytesDeduplicated) << endl;
        
        if (stats.totalBytes > 0) {
            double dedupePercent = (stats.bytesDeduplicated * 100.0) / stats.totalBytes;
            double compressionRatio = ((stats.totalBytes - stats.bytesDeduplicated) * 100.0) / stats.totalBytes;
            cout << "Deduplication rate:   " << fixed << setprecision(1) << dedupePercent << "%" << endl;
            cout << "Compression ratio:    " << compressionRatio << "%" << endl;
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
    
    if (argc >= 3) {
        source = argv[1];
        dest = argv[2];
    } else {
        cout << "Enter source directory path: ";
        getline(cin, source);
        
        cout << "Enter destination directory path: ";
        getline(cin, dest);
    }

    if (source.empty() || dest.empty()) {
        cerr << "ERROR: Source and destination paths are required!" << endl;
        cout << "\nUsage: backup.exe <source_path> <dest_path>" << endl;
        cout << "Example: backup.exe C:\\MyDocuments D:\\Backup" << endl;
        return 1;
    }

    DeduplicationBackup backup(source, dest);
    bool success = backup.StartBackup();
    
    if (success) {
        cout << "\nBackup completed successfully!" << endl;
        return 0;
    } else {
        cout << "\nBackup completed with errors!" << endl;
        return 1;
    }
}