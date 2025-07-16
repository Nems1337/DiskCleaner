# DiskCleaner v2.4.0c

A high-performance Windows disk cleanup utility designed for maximum speed and safety. DiskCleaner removes temporary files, caches, and other unnecessary data to free up disk space and improve system performance.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)
![Version](https://img.shields.io/badge/version-2.4.0c-green.svg)

## 🚀 Features

### Core Functionality
- **Ultra-fast parallel cleanup** - Uses advanced threading for 5-10x faster execution
- **Safe dry-run mode** - Preview what will be deleted without making changes
- **Custom directory management** - Add your own directories for cleanup
- **Real-time progress tracking** - Monitor cleanup progress with detailed statistics
- **Administrator privilege handling** - Automatic elevation for system file access
- **Recycle Bin integration** - Direct recycle bin emptying capability

### Built-in Cleanup Targets
- **System Temporary Files** - Windows\Temp, user temp folders
- **Application Caches** - Browser caches (Chrome, Edge, Firefox)
- **Windows Components** - Prefetch, SoftwareDistribution, thumbnails
- **Error Reports** - Windows Error Reporting files
- **Log Files** - System logs and event logs
- **Memory Dumps** - Crash dump files
- **Font Cache** - Windows font cache files

### Advanced Features
- **Custom Directory Support** - Add any directory for cleanup
- **Persistent Settings** - Custom directories saved between sessions
- **Verbose Logging** - Detailed operation reporting
- **Timeout Protection** - Prevents hanging on problematic directories
- **Atomic Operations** - Thread-safe cleanup with guaranteed completion

## 📋 System Requirements

- **Operating System**: Windows 10/11 (64-bit recommended)
- **Architecture**: x86 or x64
- **RAM**: 512 MB minimum, 1 GB recommended
- **Disk Space**: 5 MB for installation
- **Privileges**: Administrator rights for system file cleanup

## 🛠️ Installation & Building

### Pre-built Executable
1. Download the latest release from the releases page
2. Run `DiskCleaner.exe`
3. Allow administrator privileges when prompted

### Building from Source

#### Prerequisites
- Microsoft Visual Studio 2019+ or Build Tools
- Windows SDK 10.0 or later
- C++17 compatible compiler

#### Build Steps
```batch
# Clone the repository
git clone https://github.com/your-username/DiskCleaner.git
cd DiskCleaner

# Build the project
build.bat

# Run the executable
DiskCleaner.exe
```

The `build.bat` script includes:
- Animated build progress indicator
- Optimized compilation flags
- Error handling and cleanup

## 🖥️ Usage

### GUI Mode (Default)
1. **Launch the application** - Run `DiskCleaner.exe`
2. **Review cleanup items** - Check/uncheck items in the list
3. **Select options**:
   - **Dry Run**: Preview changes without deleting
   - **Verbose**: Enable detailed logging
4. **Start cleanup** - Click "Start Cleanup" button
5. **Monitor progress** - Watch the progress bar and results area

### Adding Custom Directories
1. Go to **File → Add Directory...**
2. Browse and select the directory to add
3. The directory will appear with a 🔧 icon
4. Custom directories are automatically saved

### Removing Custom Directories
1. Select a custom directory (marked with 🔧)
2. Go to **File → Remove Selected Directory**
3. Confirm the removal

## ⚡ Performance Features

### TURBO Mode (v2.3.0+)
- **Detached threading** - All cleanup tasks run in parallel
- **Maximum thread utilization** - Uses hardware_concurrency() × 2 threads
- **100ms polling** - Fast progress updates without blocking
- **Atomic counters** - Thread-safe progress tracking
- **15-second timeout** - Prevents hanging on problematic directories

### Optimization Techniques
- **Pre-filtering** - Skips empty/inaccessible directories
- **Batch processing** - Efficient file deletion in batches
- **Minimal error checking** - Optimized for speed during deletion
- **Fast size calculation** - Parallel directory size computation

## 🔧 Configuration

### Custom Directories File
Custom directories are stored in `custom_dirs.txt` with the format:
```
Directory Name|Full Path|Description|Enabled(1/0)
```

### Version System
Format: `MAJOR.MINOR.PATCH[SUFFIX]`
- **MAJOR**: Breaking changes or major features
- **MINOR**: Significant new features
- **PATCH**: Bug fixes and improvements
- **SUFFIX**: Hotfixes ("a", "b", "c") or stable ("")

## 📊 Technical Architecture

### Threading Model
- **Main Thread**: UI updates and user interaction
- **Worker Threads**: Parallel file deletion and size calculation
- **Detached Threads**: Background cleanup operations
- **Mutex Protection**: Thread-safe logging and progress updates

### Safety Features
- **Dry Run Mode**: Complete simulation without file deletion
- **Administrator Checks**: Automatic privilege elevation
- **Error Handling**: Graceful handling of access denied errors
- **Timeout Protection**: Prevents infinite hangs

### File Operations
- **Parallel Deletion**: Multiple threads for maximum I/O throughput
- **Atomic Progress**: Thread-safe progress reporting
- **Error Recovery**: Continue operation despite individual file failures

## 🐛 Troubleshooting

### Common Issues

**Buttons not responding:**
- Ensure you're running the latest version (v2.4.0b+)
- The WM_COMMAND handling was fixed in recent versions

**Cleanup gets stuck:**
- Version 2.3.0+ includes timeout protection
- Tasks automatically abandon after 15 seconds

**Permission errors:**
- Run as Administrator for system file access
- The application will prompt for elevation automatically

**Custom directories not saving:**
- Ensure the application has write permissions in its directory
- Check that `custom_dirs.txt` exists and is writable

### Debug Mode
Enable verbose logging to see detailed operation information:
1. Check "Verbose" option before cleanup
2. Monitor the results area for detailed logs
3. Report any errors with the verbose output

## 📈 Version History

### v2.4.0c (Current)
- Fixed button click handling in WM_COMMAND message processing
- Added custom directory management with File menu
- Improved directory persistence system
- Enhanced UI with custom directory indicators (🔧)

### v2.3.0 TURBO
- Revolutionary detached threading system
- 5-10x performance improvement
- Eliminated all futures-based blocking
- Ultra-fast parallel execution

### v2.2.0
- Simplified sequential processing
- Improved timeout handling
- Better error recovery

### v2.1.x Series
- Progress tracking improvements
- Batch processing optimization
- Race condition fixes

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Guidelines
- Follow C++17 standards
- Maintain thread safety in all operations
- Include appropriate error handling
- Test with both GUI and administrator privileges
- Update version numbers following the established pattern

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## ⚠️ Disclaimer

**USE AT YOUR OWN RISK**: This application permanently deletes files from your system. While it includes safety features like dry-run mode and focuses on temporary files, always:

1. **Use dry-run mode first** to preview what will be deleted
2. **Backup important data** before running cleanup
3. **Review the cleanup list** to ensure only intended items are selected
4. **Test on non-critical systems** before production use

The developers are not responsible for any data loss resulting from the use of this application.

## 🙋‍♂️ Support

- **Issues**: Report bugs via GitHub Issues
- **Discussions**: Use GitHub Discussions for questions
- **Documentation**: This README and inline code comments

---

**DiskCleaner** - Fast, Safe, Powerful Windows Disk Cleanup 