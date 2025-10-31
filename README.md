# 🗂️ Incremental File Backup System with Deduplication

A high-performance file backup utility written in C++ that implements intelligent incremental backups with content-addressable deduplication, achieving up to **90% space savings** in real-world scenarios.

![C++](https://img.shields.io/badge/C%2B%2B-14-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)

## 📋 Table of Contents

- [Overview](#-overview)
- [Key Features](#-key-features)
- [Architecture](#-architecture)
- [How It Works](#-how-it-works)
- [Installation](#-installation)
- [Usage](#-usage)
- [Project Phases](#-project-phases)
- [Performance](#-performance)
- [Technical Details](#-technical-details)
- [Future Enhancements](#-future-enhancements)
- [Contributing](#-contributing)
- [License](#-license)

## 🎯 Overview

This project is a comprehensive backup solution that evolved through three phases, demonstrating advanced concepts in systems programming, file I/O, cryptographic hashing, and data deduplication. Built as a learning project to understand enterprise backup systems like **Veritas NetBackup** and **Commvault**.

### 🌟 Why This Project?

Enterprise backup solutions face a fundamental challenge: backing up the same data repeatedly is inefficient and wasteful. This tool addresses these issues through:

- **Incremental Backup**: Only copy changed files
- **Content Deduplication**: Store identical content once
- **SHA-256 Hashing**: Detect changes at the byte level
- **Space Optimization**: Achieve 60-90% storage savings

## ✨ Key Features

### 🔄 Phase 1: Full Backup
- ✅ Recursive directory traversal
- ✅ Preserves folder structure
- ✅ Error handling and reporting
- ✅ Detailed statistics

### ⚡ Phase 2: Incremental Backup
- ✅ SHA-256 file hashing
- ✅ Manifest-based change detection
- ✅ Skips unchanged files
- ✅ 10-100x faster subsequent backups

### 🎯 Phase 3: Deduplication
- ✅ Content-addressable storage
- ✅ Eliminates duplicate content
- ✅ Reference counting
- ✅ 60-90% space savings

## 🏗️ Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                     File Backup System                       │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Source Files              Backup Process                    │
│  ┌──────────┐             ┌──────────────┐                 │
│  │ file1.txt│─────────────▶│  Calculate   │                 │
│  │ file2.txt│             │  SHA-256     │                 │
│  │ file3.txt│             │  Hash        │                 │
│  └──────────┘             └──────┬───────┘                 │
│                                  │                          │
│                                  ▼                          │
│                          ┌──────────────┐                  │
│                          │  Check if    │                  │
│                          │  content     │                  │
│                          │  exists      │                  │
│                          └──────┬───────┘                  │
│                                  │                          │
│                    ┌─────────────┴─────────────┐           │
│                    ▼                           ▼           │
│            ┌──────────────┐           ┌──────────────┐    │
│            │   [NEW]      │           │   [DEDUP]    │    │
│            │ Store content│           │ Reference    │    │
│            │ in .dedup_   │           │ existing     │    │
│            │ store        │           │ content      │    │
│            └──────────────┘           └──────────────┘    │
│                    │                           │           │
│                    └─────────────┬─────────────┘           │
│                                  ▼                          │
│                          ┌──────────────┐                  │
│                          │  Update      │                  │
│                          │  .dedup_     │                  │
│                          │  index.txt   │                  │
│                          └──────────────┘                  │
└─────────────────────────────────────────────────────────────┘

Storage Structure:
├── .dedup_store/
│   ├── abc123...bin  (actual content)
│   └── def456...bin  (actual content)
└── .dedup_index.txt  (filename → hash mapping)
```

## 🔍 How It Works

### Content-Addressable Storage

Instead of storing files by name, files are stored by their content hash:
```
Traditional Backup:
photo1.jpg → Backup/photo1.jpg (1.5 MB)
photo2.jpg → Backup/photo2.jpg (1.5 MB) ← Duplicate!
Total: 3 MB

Deduplication Backup:
photo1.jpg → hash_abc → .dedup_store/abc.bin (1.5 MB)
photo2.jpg → hash_abc → .dedup_store/abc.bin (same file!)
Total: 1.5 MB (50% saved!)
```

### SHA-256 Hashing

Every file gets a unique 256-bit fingerprint:
- Same content = Same hash (always)
- Different content = Different hash (always)
- Collision probability: 1 in 2^256 (practically impossible)

### Reference System

The `.dedup_index.txt` maps filenames to content hashes:
```
file1.txt|a3f5e8d9c7b2...
file2.txt|a3f5e8d9c7b2...  ← Same hash = shared content
file3.txt|d7c9b2f4e1a5...
```

## 📥 Installation

### Prerequisites

- **Windows OS** (Windows 7 or later)
- **GCC/MinGW** compiler
- **C++14** or later

### Build Instructions
```bash
# Clone the repository
git clone https://github.com/yourusername/file-backup-system.git
cd file-backup-system

# Compile Phase 3 (Deduplication)
g++ -std=c++14 phase3.cpp -o backup.exe -ladvapi32

# Or use the provided build script
build.bat
```

## 🚀 Usage

### Basic Backup
```bash
# Full backup
backup.exe C:\Documents D:\Backup

# With spaces in paths
backup.exe "C:\My Documents" "D:\My Backup"
```

### Example Output
```
========================================
  FILE BACKUP TOOL - Phase 3
  Deduplication Enabled
========================================
Source: C:\TestPhotos\
Destination: D:\Backup\
========================================

  [NEW] C:\TestPhotos\photo1.jpg
  [DEDUP] C:\TestPhotos\photo2.jpg (already stored)
  [DEDUP] C:\TestPhotos\photo3.jpg (already stored)

========================================
  BACKUP COMPLETE
========================================
Files processed:      3
Files copied:         1 (new content)
Files deduplicated:   2 (shared content)
Directories created:  1
Errors:               0

Storage Analysis:
Total source size:    4.59 MB
Actual data stored:   1.53 MB
Space saved (dedup):  3.06 MB
Deduplication rate:   66.7%
========================================

Backup completed successfully!
```

## 📊 Project Phases

### Phase 1: Basic Full Backup
**Goal**: Implement recursive directory copying

**Features**:
- Recursive directory traversal using Windows API
- File copying with `CopyFile` API
- Error handling and statistics
- Command-line interface

**Code**: `phase1.cpp` | **Lines**: ~250

---

### Phase 2: Incremental Backup
**Goal**: Skip unchanged files using hashing

**Features**:
- SHA-256 file hashing via Windows Crypto API
- Manifest file for tracking previous backups
- Change detection (NEW/MODIFIED/UNCHANGED)
- Enhanced statistics

**Code**: `phase2.cpp` | **Lines**: ~450

---

### Phase 3: Deduplication
**Goal**: Store identical content only once

**Features**:
- Content-addressable storage
- Deduplication store and index
- Reference counting
- Space savings metrics

**Code**: `phase3.cpp` | **Lines**: ~600

## 📈 Performance

### Real-World Test Results

**Scenario**: Backing up 10 identical photos (1.5 MB each)

| Metric | Phase 1 | Phase 3 |
|--------|---------|---------|
| **Total source size** | 15 MB | 15 MB |
| **Storage used** | 15 MB | 1.5 MB |
| **Space saved** | 0% | **90%** |
| **Files stored** | 10 | 1 |
| **Backup time (2nd run)** | ~10 sec | ~2 sec |

### Enterprise Scenario

**1000 employees with identical company files:**
```
Without Deduplication:
- Company logo (500 KB) × 1000 = 500 MB
- Employee handbook (10 MB) × 1000 = 10 GB
- Total: ~10.5 GB

With Deduplication:
- Company logo: 500 KB (stored once)
- Employee handbook: 10 MB (stored once)
- Total: ~10.5 MB

Space saved: 99%! (10.5 GB → 10.5 MB)
```

## 🔧 Technical Details

### Technologies Used

- **Language**: C++ (C++14 standard)
- **Platform**: Windows (Win32 API)
- **Compiler**: MinGW GCC 6.3.0+
- **Cryptography**: Windows Crypto API (SHA-256)

### Windows APIs
```cpp
// File System
FindFirstFile / FindNextFile  // Directory traversal
CreateDirectory              // Folder creation
CopyFile                     // File copying
GetFileAttributes           // File metadata

// Cryptography
CryptAcquireContext  // Initialize crypto provider
CryptCreateHash      // Create hash object
CryptHashData        // Hash file data
CryptGetHashParam    // Get hash result
```

### Key Algorithms

**Recursive Directory Traversal**:
- Time Complexity: O(N) where N = number of files
- Space Complexity: O(D) where D = directory depth
- Uses depth-first search

**Deduplication**:
- Hash Calculation: O(N × S) where S = average file size
- Duplicate Detection: O(1) hash lookup
- Storage: O(U) where U = unique content blocks

### Data Structures
```cpp
// Deduplication Index
map<string, string> fileHashMap;  // filename → hash

// Reference Counter
map<string, int> referenceCount;  // hash → count

// Statistics
struct BackupStats {
    int filesProcessed;
    int filesCopied;
    int filesDeduped;
    long long bytesDeduplicated;
};
```

## 🎓 Learning Outcomes

This project demonstrates understanding of:

- ✅ **Systems Programming**: Low-level Windows API, file I/O
- ✅ **Cryptography**: SHA-256 hashing, content verification
- ✅ **Algorithms**: Recursion, tree traversal, hash maps
- ✅ **Data Structures**: Maps, reference counting
- ✅ **Software Design**: OOP, class encapsulation, separation of concerns
- ✅ **Error Handling**: Windows error codes, exception safety
- ✅ **Performance Optimization**: Memory-efficient file processing

## 🚀 Future Enhancements

### Potential Features

- [ ] **Compression**: Integrate zlib for content compression
- [ ] **Multi-threading**: Parallel file hashing and copying
- [ ] **Encryption**: AES-256 encryption for sensitive data
- [ ] **Cloud Integration**: Upload to Google Drive, OneDrive
- [ ] **GUI**: Qt-based graphical interface
- [ ] **Scheduling**: Automatic periodic backups
- [ ] **Restore Tool**: Dedicated restoration utility
- [ ] **Block-level Deduplication**: Chunking for large files
- [ ] **Incremental Forever**: Chain of incremental backups
- [ ] **Network Backup**: Remote server support

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

### Areas for Contribution

- Cross-platform support (Linux, macOS)
- Performance optimizations
- Additional compression algorithms
- Unit tests
- Documentation improvements

## 👨‍💻 Author

**Amol Raut**
- GitHub: [@Amolraut638](https://github.com/Amolraut638)
- LinkedIn: [amolraut9272](https://linkedin.com/in/amolraut9272)
- Email: amolraut1902@gmail.com

## 🙏 Acknowledgments

- Inspired by enterprise backup solutions like Veritas NetBackup
- Windows Crypto API documentation
- C++ systems programming community

---

⭐ **Star this repo if you found it helpful!**

---

<p align="center">
  Made with ❤️ and C++
</p>
