#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "GenericPlatform/GenericPlatformFile.h"

// Add these defines before including zlib
#define __GLIBC_USE_ISOC23 1
#define __GLIBC_USE_IEC_60559_BFP_EXT_C23 1
#include "ThirdParty/zlib/1.3/include/zlib.h"

class PROXSIMA_API FZipHandlingUtility
{
public:
    static constexpr uint32 CHUNK_SIZE = 16384;  // 16KB chunks
    static constexpr uint32 MAX_FILENAME_LENGTH = 512;
    static constexpr uint32 LOCAL_HEADER_SIGNATURE = 0x04034b50;
    static constexpr uint32 CENTRAL_DIRECTORY_SIGNATURE = 0x02014b50;
    static constexpr uint32 END_OF_CENTRAL_DIR_SIGNATURE = 0x06054b50;

    #pragma pack(push, 1)
    struct LocalFileHeader
    {
        uint32_t Signature;           // 0x04034b50
        uint16_t VersionNeeded;
        uint16_t Flags;
        uint16_t CompressionMethod;
        uint16_t LastModTime;
        uint16_t LastModDate;
        uint32_t Crc32;
        uint32_t CompressedSize;
        uint32_t UncompressedSize;
        uint16_t FileNameLength;
        uint16_t ExtraFieldLength;
    };
    #pragma pack(pop)

    static bool UnzipFile(const FString& ZipFilePath, const FString& DestinationPath);

private:
    static bool ExtractFile(
        const TArray<uint8> &ZipData,
        int64 &CurrentPosition,
        const FString &DestinationPath,
        const LocalFileHeader &Header);

    static bool DecompressData(
        const uint8* CompressedData,
        const uint32 CompressedSize,
        uint8* UncompressedData,
        const uint32 UncompressedSize
    );
};
