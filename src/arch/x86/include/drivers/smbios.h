#ifndef SMBIOS_H
#define SMBIOS_H

struct SMBIOSEntryPoint {
    char           EntryPointString[4];
    unsigned char  Checksum;
    unsigned char  Length;
    unsigned char  MajorVersion;
    unsigned char  MinorVersion;
    unsigned short MaxStructureSize;
    unsigned char  EntryPointRevision;
    char           FormattedArea[5];
    char           EntryPointString2[5];
    unsigned char  Checksum2;
    unsigned short TableLength;
    unsigned int   TableAddress;
    unsigned short NumberOfStructures;
    unsigned char  BCDRevision;
};

struct SMBIOSHeader {
    unsigned char  Type;
    unsigned char  Length;
    unsigned short Handle;
} __attribute__((packed));

struct ExtendedBiosROMSize {
    unsigned short Size : 14;
    unsigned short Unit : 2;
} __attribute__((packed));

struct BIOS_Info // Type 0
{
    struct SMBIOSHeader        header;
    unsigned char              Vendor;
    unsigned char              Version;
    unsigned short             StartAddr;
    unsigned char              ReleaseDate;
    unsigned char              ROMSize;
    unsigned char              Characteristics[8];
    unsigned char              CharacteristicsExt[2];
    unsigned char              MajorRelease;
    unsigned char              MinorRelease;
    unsigned char              EmveddedControllerFirmwareMajorRelease;
    unsigned char              EmveddedControllerFirmwareMinorRelease;
    struct ExtendedBiosROMSize ExtendedBiosSize;
} __attribute__((packed));

struct System_info // Type 1
{
    struct SMBIOSHeader header;
    unsigned char       Manufacturer;
    unsigned char       ProductName;
    unsigned char       Version;
    unsigned char       SerialNumver;
    unsigned char       UUID[16];
    unsigned char       WakeupType;
    unsigned char       SKUNumber;
    unsigned char       Family;
} __attribute__((packed));

struct Baseboard_info // Type 2
{
    struct SMBIOSHeader header;
    unsigned char       Manufacturer;
    unsigned char       ProductName;
    unsigned char       Version;
    unsigned char       SerialNumver;
    unsigned char       AssetTag;
    unsigned char       FeatureFlags;
    unsigned char       LocationInChassis;
    unsigned short      ChassisHandle;
    unsigned char       BoardType;
    unsigned char       NumberOfContainedObjectHandles;
    unsigned short      ContainedObjectHandles;
} __attribute__((packed));

struct Processor_info // Type 4
{
    struct SMBIOSHeader header;
    unsigned char       SocketDesignation;
    unsigned char       ProcessorType;
    unsigned char       ProcessorFamily;
    unsigned char       ProcessorManufacturer;
    unsigned int        ProcessorID[2];
    unsigned char       ProcessorVersion;
    unsigned char       Voltage;
    unsigned short      ExternalClock;
    unsigned short      MaxSpeed;
    unsigned short      CurrentSpeed;
    unsigned char       Status;
    unsigned char       ProcessorUpgrade;
    unsigned short      L1CacheHandle;
    unsigned short      L2CacheHandle;
    unsigned short      L3CacheHandle;
    unsigned char       SerialNumber;
    unsigned char       AssetTag;
    unsigned char       PartNumber;
    unsigned char       CoreCount;
    unsigned char       CoreEnabled;
    unsigned char       ThreadCount;
    unsigned short      ProcessorCharacteristics;
    unsigned short      ProcessorFamily2;
    unsigned short      CoreCount2;
    unsigned short      CoreEnabled2;
    unsigned short      ThreadCount2;
} __attribute__((packed));

extern unsigned int smbios_tables[256];

void init_smbios(void);

#endif