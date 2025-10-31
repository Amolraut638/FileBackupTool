#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>

// MinGW compatibility
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#endif

using namespace std;

// Statistics structure
struct BackupStats {
    int filesProcessed = 0;
    int filesCopied = 0;
    int directoriesCreated = 0;
    int errors = 0;
    long long totalBytes = 0;
};

class FileBackup {
private:
    string sourcePath;
    string destPath;
    BackupStats stats;

    // Convert error code to string
    string GetLastErrorAsString() {
        DWORD errorCode = GetLastError();
        if (errorCode == 0) return "No error";
        
        LPSTR buffer = nullptr;
        size_t size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&buffer, 0, NULL);
        
        string message(buffer, size);
        LocalFree(buffer);
        return message;
    }

    // Ensure path ends with backslash
    string NormalizePath(const string& path) {
        string normalized = path;
        if (!normalized.empty() && normalized.back() != '\\') {
            normalized += '\\';
        }
        return normalized;
    }

    // Create destination directory structure
    bool CreateDestDirectory(const string& path) {
        // Check if directory already exists
        DWORD attribs = GetFileAttributesA(path.c_str());
        if (attribs != INVALID_FILE_ATTRIBUTES && 
            (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
            return true; // Already exists
        }

        // Create directory
        if (CreateDirectoryA(path.c_str(), NULL)) {
            stats.directoriesCreated++;
            return true;
        }
        
        DWORD error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) {
            return true;
        }
        
        // Try to create parent directories recursively
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

    // Copy single file
    bool CopyFileWithProgress(const string& source, const string& dest) {
        cout << "  Copying: " << source << endl;
        
        if (CopyFileA(source.c_str(), dest.c_str(), FALSE)) {
            stats.filesCopied++;
            return true;
        } else {
            cerr << "  ERROR: Failed to copy - " << GetLastErrorAsString() << endl;
            stats.errors++;
            return false;
        }
    }

    // Get file size
    long long GetFileSize(WIN32_FIND_DATAA& findData) {
        LARGE_INTEGER size;
        size.LowPart = findData.nFileSizeLow;
        size.HighPart = findData.nFileSizeHigh;
        return size.QuadPart;
    }

    // Recursive backup function
    bool BackupDirectory(const string& sourceDir, const string& destDir) {
        string searchPath = sourceDir + "*";
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
        
        if (hFind == INVALID_HANDLE_VALUE) {
            cerr << "ERROR: Cannot access directory: " << sourceDir << endl;
            stats.errors++;
            return false;
        }

        // Create destination directory
        if (!CreateDestDirectory(destDir)) {
            cerr << "ERROR: Cannot create directory: " << destDir << endl;
            stats.errors++;
            FindClose(hFind);
            return false;
        }

        do {
            string fileName = findData.cFileName;
            
            // Skip "." and ".."
            if (fileName == "." || fileName == "..") {
                continue;
            }

            string sourceFullPath = sourceDir + fileName;
            string destFullPath = destDir + fileName;
            
            stats.filesProcessed++;

            // Check if it's a directory
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                cout << "\nEntering directory: " << sourceFullPath << endl;
                BackupDirectory(sourceFullPath + "\\", destFullPath + "\\");
            } else {
                // It's a file - copy it
                long long fileSize = GetFileSize(findData);
                stats.totalBytes += fileSize;
                
                if (CopyFileWithProgress(sourceFullPath, destFullPath)) {
                    // Success
                }
            }
            
        } while (FindNextFileA(hFind, &findData));

        FindClose(hFind);
        return true;
    }

public:
    FileBackup(const string& src, const string& dst) {
        sourcePath = NormalizePath(src);
        destPath = NormalizePath(dst);
    }

    // Start backup process
    bool StartBackup() {
        cout << "========================================" << endl;
        cout << "  FILE BACKUP TOOL - Phase 1" << endl;
        cout << "========================================" << endl;
        cout << "Source: " << sourcePath << endl;
        cout << "Destination: " << destPath << endl;
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
        
        // Print statistics
        PrintStats();
        
        return result;
    }

    // Print backup statistics
    void PrintStats() {
        cout << "\n========================================" << endl;
        cout << "  BACKUP COMPLETE" << endl;
        cout << "========================================" << endl;
        cout << "Files processed:      " << stats.filesProcessed << endl;
        cout << "Files copied:         " << stats.filesCopied << endl;
        cout << "Directories created:  " << stats.directoriesCreated << endl;
        cout << "Errors:               " << stats.errors << endl;
        cout << "Total size:           " << FormatBytes(stats.totalBytes) << endl;
        cout << "========================================" << endl;
    }

    // Format bytes to human-readable
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

// Main function
int main(int argc, char* argv[]) {
    // Simple command-line parsing
    string source, dest;
    
    if (argc >= 3) {
        source = argv[1];
        dest = argv[2];
    } else {
        // Interactive mode
        cout << "Enter source directory path: ";
        getline(cin, source);
        
        cout << "Enter destination directory path: ";
        getline(cin, dest);
    }

    // Validate input
    if (source.empty() || dest.empty()) {
        cerr << "ERROR: Source and destination paths are required!" << endl;
        cout << "\nUsage: backup.exe <source_path> <dest_path>" << endl;
        cout << "Example: backup.exe C:\\MyDocuments D:\\Backup" << endl;
        return 1;
    }

    // Create backup object and start
    FileBackup backup(source, dest);
    bool success = backup.StartBackup();
    
    if (success) {
        cout << "\nBackup completed successfully!" << endl;
        return 0;
    } else {
        cout << "\nBackup completed with errors!" << endl;
        return 1;
    }
}