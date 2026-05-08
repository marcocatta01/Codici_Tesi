#include "ZipHandlingUtility.h"

bool FZipHandlingUtility::UnzipFile(const FString &ZipFilePath, const FString &DestinationPath)
{
    // Read the entire zip file into memory
    TArray<uint8> ZipData;
    if (!FFileHelper::LoadFileToArray(ZipData, *ZipFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load zip file: %s"), *ZipFilePath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("Successfully loaded zip file: %s (Size: %d bytes)"), *ZipFilePath, ZipData.Num());

    // Clean existing directory first
    IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (PlatformFile.DirectoryExists(*DestinationPath))
    {
        UE_LOG(LogTemp, Log, TEXT("Cleaning existing extraction directory: %s"), *DestinationPath);
        if (!PlatformFile.DeleteDirectoryRecursively(*DestinationPath))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to clean existing directory: %s"), *DestinationPath);
            return false;
        }
    }

    // Create fresh directory
    if (!PlatformFile.CreateDirectoryTree(*DestinationPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create destination directory: %s"), *DestinationPath);
        return false;
    }

    int64 CurrentPosition = 0;
    const int64 FileSize = ZipData.Num();
    int32 FileCount = 0;

    // Process each file in the zip
    while (static_cast<size_t>(CurrentPosition) < FileSize - sizeof(LocalFileHeader))
    {
        // Read local file header
        LocalFileHeader Header;
        FMemory::Memcpy(&Header, &ZipData[CurrentPosition], sizeof(LocalFileHeader));

        // Verify header signature
        if (Header.Signature != LOCAL_HEADER_SIGNATURE)
        {
            // We might have reached the central directory
            if (Header.Signature == CENTRAL_DIRECTORY_SIGNATURE ||
                Header.Signature == END_OF_CENTRAL_DIR_SIGNATURE)
            {
                UE_LOG(LogTemp, Log, TEXT("Reached end of zip content. Processed %d files."), FileCount);
                break; // Normal end of processing
            }

            UE_LOG(LogTemp, Error, TEXT("Invalid zip file header signature at position %lld"), CurrentPosition);
            return false;
        }

        CurrentPosition += sizeof(LocalFileHeader);
		
        // Read filename
        TArray<ANSICHAR> FileNameBuffer;
        FileNameBuffer.SetNum(Header.FileNameLength + 1);
        FMemory::Memcpy(FileNameBuffer.GetData(), &ZipData[CurrentPosition], Header.FileNameLength);
        FileNameBuffer[Header.FileNameLength] = '\0';

        FString FileName = UTF8_TO_TCHAR(FileNameBuffer.GetData());
        CurrentPosition += Header.FileNameLength;

        UE_LOG(LogTemp, Log, TEXT("Processing file: %s (Compressed: %d bytes, Uncompressed: %d bytes)"),
               *FileName, Header.CompressedSize, Header.UncompressedSize);

        // Skip extra field
        CurrentPosition += Header.ExtraFieldLength;

        // Extract the file
        if (!ExtractFile(ZipData, CurrentPosition, DestinationPath / FileName, Header))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to extract file: %s"), *FileName);
            return false;
        }

        FileCount++;
        // Move to next file
        CurrentPosition += Header.CompressedSize;
    }

    UE_LOG(LogTemp, Log, TEXT("Successfully extracted all files to: %s"), *DestinationPath);
    return true;
}

bool FZipHandlingUtility::ExtractFile(
    const TArray<uint8> &ZipData,
    int64 &CurrentPosition,
    const FString &DestinationPath,
    const LocalFileHeader &Header)
{
    // Skip if this is a directory entry (ends with '/')
    if (DestinationPath.EndsWith(TEXT("/")))
    {
        // Just create the directory and return success
        IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        return PlatformFile.CreateDirectoryTree(*DestinationPath);
    }

    // Create directories if needed
    FString Directory = FPaths::GetPath(DestinationPath);
    if (!Directory.IsEmpty())
    {
        IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.CreateDirectoryTree(*Directory))
        {
            return false;
        }
    }

    // Allocate memory for uncompressed data
    TArray<uint8> UncompressedData;
    UncompressedData.SetNum(Header.UncompressedSize);

    // Extract the file data
    if (Header.CompressionMethod == 0) // No compression
    {
        // Just copy the data
        FMemory::Memcpy(UncompressedData.GetData(), &ZipData[CurrentPosition], Header.UncompressedSize);
    }
    else if (Header.CompressionMethod == 8) // DEFLATE compression
    {
        if (!DecompressData(
                &ZipData[CurrentPosition],
                Header.CompressedSize,
                UncompressedData.GetData(),
                Header.UncompressedSize))
        {
            return false;
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Unsupported compression method: %d"), Header.CompressionMethod);
        return false;
    }

    // Write the file
    return FFileHelper::SaveArrayToFile(UncompressedData, *DestinationPath);
}

bool FZipHandlingUtility::DecompressData(
    const uint8 *CompressedData,
    const uint32 CompressedSize,
    uint8 *UncompressedData,
    const uint32 UncompressedSize)
{
    z_stream Stream = {};
    Stream.next_in = const_cast<Bytef *>(CompressedData);
    Stream.avail_in = CompressedSize;
    Stream.next_out = UncompressedData;
    Stream.avail_out = UncompressedSize;

    // Initialize for DEFLATE decompression
    if (inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
    {
        return false;
    }

    // Decompress
    int Result = inflate(&Stream, Z_FINISH);
    inflateEnd(&Stream);

    return (Result == Z_STREAM_END);
}