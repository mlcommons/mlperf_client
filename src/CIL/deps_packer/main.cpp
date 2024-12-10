#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono;

static int addFile(const int maxCppSize, const fs::path& filePath,
                   const std::string& classname, std::ofstream& headerStream,
                   std::vector<std::ofstream>& cppStreams, int& currentCppIdx,
                   int& availableSize) {
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << filePath << std::endl;
    return 0;
  }

  auto fileSize = static_cast<int>(file.tellg());
  auto remainingSize = fileSize;
  file.seekg(0, std::ios::beg);

  std::string name = filePath.filename().string();
  std::replace(name.begin(), name.end(), ' ', '_');
  std::replace(name.begin(), name.end(), '.', '_');

  std::cout << "Adding " << name << ": " << fileSize << " bytes ";

  static int tempNum = 0;
  int part = 0;

  auto startFilling = high_resolution_clock::now();

  std::vector<unsigned> chunkSizes;

  bool etaPrinted = false;

  // Calculate time to fill the file

  while (remainingSize > 0) {
    int chunkSize = std::min(availableSize, remainingSize);
    std::vector<char> buffer(chunkSize);
    if (!file.read(buffer.data(), chunkSize)) {
      std::cerr << "\nFailed to read file: " << filePath << std::endl;
      return 0;
    }

    // Use the current cpp file from the vector
    std::ofstream& cppStream = cppStreams[currentCppIdx];

    cppStream << "const unsigned char " << classname << "::temp" << ++tempNum
              << "[] = {";
    for (size_t i = 0; i < static_cast<size_t>(chunkSize); ++i) {
      if ((i % 40) != 39)
        cppStream << static_cast<unsigned int>(
                         static_cast<unsigned char>(buffer[i]))
                  << ",";
      else
        cppStream << static_cast<unsigned int>(
                         static_cast<unsigned char>(buffer[i]))
                  << ",\n  ";
    }
    cppStream << "0};\n";
    chunkSizes.push_back(chunkSize);

    remainingSize -= chunkSize;
    availableSize -= chunkSize;
    part++;

    auto percentage =
        (int)std::ceil(((double)(fileSize - remainingSize) / fileSize) * 100.0);

    auto endFilling = high_resolution_clock::now();

    auto duration =
        duration_cast<milliseconds>(endFilling - startFilling).count() / 1000.0;

    auto totalTime = duration * 100.0 / percentage;

    auto remaining = totalTime - duration;
    auto minutesLeft = (int)remaining / 60;
    auto secondsLeft = (int)remaining % 60;

    if (!etaPrinted) {
      std::cout << "ETA: " << minutesLeft << " minutes " << secondsLeft
                << " seconds" << std::endl;
	}

    if (etaPrinted && (percentage % 10) == 0)
        std::cout << "Progress: " << percentage << "%, ETA: " << minutesLeft
                << " minutes " << secondsLeft << " seconds" << std::endl;

    etaPrinted = true;

    // Check if we need to move to the next cpp file
    if (availableSize <= 0 || remainingSize > 0) {
      // Move to the next cpp file and reset availableSize
      currentCppIdx = (currentCppIdx + 1) % cppStreams.size();
      availableSize = maxCppSize;
      cppStream.flush();
    }
  }

  std::cout << std::endl;

  auto& lastCpp = cppStreams[currentCppIdx];

  lastCpp << "const unsigned char* " << classname << "::" << name << "[] = {";

  for (int i = 0; i < part; ++i) {
    lastCpp << "reinterpret_cast<const unsigned char*>(temp"
            << (tempNum - part + i + 1) << "), ";
  }

  lastCpp << "(const unsigned char*)0};\n\n";

  for (int i = 0; i < part; ++i) {
    headerStream << " extern const unsigned char temp"
                 << (tempNum - part + i + 1) << "[];\n";
  }

  // Adjust headerStream for multiple parts
  headerStream << "    extern const unsigned char*  " << name << "[];\n"
               << "    const int           " << name << "Size = " << fileSize
               << ";\n"
               << "    const int           " << name << "Parts = " << part
               << ";\n";

  // Adjust headerStream for multiple parts
  headerStream << "    const unsigned      " << name << "PartSizes[] = {";
  for (int i = 0; i < part; i++) headerStream << chunkSizes[i] << ", ";
  headerStream << "0};\n\n";

  headerStream.flush();

  return static_cast<int>(fileSize);
}

int main(int argc, char* argv[]) {
  if (argc < 5) {  // Require at least one argument for --divide
    std::cerr << "Usage: " << argv[0]
              << " --divide <parts> <destination_directory> <class_name> "
                 "<file1> [<file2> ...]"
              << std::endl;
    std::cerr << "Provided: ";
    for (int i = 0; i < argc; ++i) {
      std::cerr << argv[i] << " ";
    }
    std::cerr << std::endl;
    return 1;
  }

  int divideIntoParts = std::stoi(argv[2]);
  fs::path destDirectory = argv[3];
  std::string className = argv[4];

  if (!fs::is_directory(destDirectory)) {
    std::cerr << "Destination directory doesn't exist: " << destDirectory
              << std::endl;
    return 1;
  }

  fs::path headerFile = destDirectory / (className + ".h");

  std::vector<fs::path> cppFiles;

  if (divideIntoParts < 2) {
    cppFiles.push_back(destDirectory / (className + ".cpp"));
    std::cout << "Creating " << headerFile << " and " << cppFiles[0] << "..."
              << std::endl
              << std::endl;
  } else {
    std::cout << "Creating " << headerFile << " and ";
    for (int i = 0; i < divideIntoParts; ++i) {
      cppFiles.push_back(destDirectory /
                         (className + std::to_string(i + 1) + ".cpp"));
      std::cout << cppFiles[0] << " ";
    }
    std::cout << cppFiles[0] << " ..." << std::endl << std::endl;
  }

  uintmax_t totalSize = 0;  // Variable to store the total size

  std::vector<fs::path> files;
  for (int i = 5; i < argc; ++i) {
    std::string file_path = argv[i];
    // Trim trailing spaces
    file_path.erase(file_path.find_last_not_of(' ') + 1);
    fs::path file = file_path;

    if (fs::exists(file) && fs::is_regular_file(file)) {
      files.push_back(file);
      // Accumulate the size of each file
      totalSize += fs::file_size(file);
    } else {
      std::cerr << "Warning: Bad file : " << file;
      if (fs::exists(file))
        std::cerr << ", exists";
      else
        std::cerr << ", doesn't exist";
      if (fs::is_regular_file(file))
        std::cerr << ", regular file";
      else
        std::cerr << ", not a regular file";
      std::cerr << std::endl;
    }
  }

  if (files.empty()) {
    std::cerr << "No valid files provided." << std::endl;
    return 1;
  }

  std::ofstream header(headerFile);

  if (!header.is_open()) {
    std::cerr << "Couldn't open output files for writing" << std::endl;
    return 1;
  }

  std::vector<std::ofstream> cppStreams;
  int currentCpp = 0;

  for (const auto& cppFile : cppFiles) {
    cppStreams.emplace_back(cppFile, std::ios::out | std::ios::trunc);
    if (!cppStreams.back().is_open()) {
      std::cerr << "Couldn't open output file for writing: " << cppFile
                << std::endl;
      return 1;
    }
  }

  header << "/* (Auto-generated binary data file). */\n\n"
         << "#pragma once\n"
         << "namespace " << className << "\n"
         << "{\n";

  for (auto& cpp : cppStreams) {
    cpp << "/* (Auto-generated binary data file). */\n\n"
        << "#include \"" << className << ".h\"\n";
  }

  int totalBytes = 0;
  int maxCppSize = (int)std::ceil((double)totalSize / divideIntoParts);
  int allowedSize = maxCppSize;
  for (const auto& file : files) {
    totalBytes += addFile(maxCppSize, file, className, header, cppStreams,
                          currentCpp, allowedSize);
  }

  header << "}\n";

  std::cout << std::endl
            << " Total size of binary data: " << totalBytes << " bytes"
            << std::endl;

  return 0;
}
