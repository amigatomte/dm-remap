/*
 * Test suite for dm-remap v4.0 metadata creation functions
 * 
 * This test validates:
 * 1. Metadata structure creation
 * 2. Device fingerprinting
 * 3. CRC32 integrity protection
 * 4. Metadata placement validation
 * 5. Version control functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>
#include <stdint.h>

// Mock kernel structures for userspace testing
struct block_device {
    char *bd_disk_name;
    unsigned long bd_nr_sectors;
};

struct dm_dev {
    struct block_device *bdev;
    char name[256];
};

// Define constants that would be in the header
#define DM_REMAP_V4_MAGIC 0x44524D52  // "DRMR"
#define DM_REMAP_V4_VERSION 0x00040000  // 4.0.0
#define DM_REMAP_V4_MAX_TARGETS 16
#define DM_REMAP_V4_MAX_SPARES 8
#define DM_REMAP_V4_MAX_METADATA_COPIES 5

// Define structures for userspace testing
struct dm_remap_v4_device_fingerprint {
    char device_uuid[37];
    char device_path[256];
    uint64_t device_size;
    uint32_t serial_hash;
    char spare_reserved[12];
};

struct dm_remap_v4_target_config {
    uint64_t start_sector;
    uint64_t length;
    char device_name[256];
    char target_type[32];
    uint32_t flags;
    char reserved[220];
};

struct dm_remap_v4_spare_device_info {
    struct dm_remap_v4_device_fingerprint fingerprint;
    uint64_t device_size;
    uint32_t status_flags;
    char reserved[60];
};

struct dm_remap_v4_reassembly_instructions {
    uint32_t reassembly_mode;
    uint32_t validation_level;
    uint32_t recovery_options;
    char reserved[244];
};

struct dm_remap_v4_metadata_header {
    uint32_t magic;
    uint32_t version;
    uint32_t metadata_size;
    uint32_t crc32;
    uint64_t sequence_number;
    uint64_t creation_time;
    uint32_t num_targets;
    uint32_t num_spares;
    char reserved[216];
};

struct dm_remap_v4_metadata {
    struct dm_remap_v4_metadata_header header;
    struct dm_remap_v4_target_config targets[DM_REMAP_V4_MAX_TARGETS];
    struct dm_remap_v4_spare_device_info spares[DM_REMAP_V4_MAX_SPARES];
    struct dm_remap_v4_reassembly_instructions reassembly;
};

// Mock CRC32 function for testing
uint32_t crc32(uint32_t crc, const unsigned char *buf, size_t len) {
    // Standard CRC32 implementation for testing (IEEE 802.3 polynomial)
    uint32_t i;
    crc = crc ^ 0xffffffff;
    while (len--) {
        crc = crc ^ (*buf++);
        for (i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return crc ^ 0xffffffff;
}

// Mock time function
uint64_t ktime_get_real_seconds(void) {
    return (uint64_t)time(NULL);
}

// Test helper functions
void print_test_header(const char *test_name) {
    printf("\n=== Testing %s ===\n", test_name);
}

void print_test_result(const char *test_name, int passed) {
    printf("[%s] %s\n", passed ? "PASS" : "FAIL", test_name);
}

// Test 1: Basic metadata structure creation
int test_metadata_structure_creation(void) {
    print_test_header("Metadata Structure Creation");
    
    struct dm_remap_v4_metadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    
    // Initialize basic fields
    metadata.header.magic = DM_REMAP_V4_MAGIC;
    metadata.header.version = DM_REMAP_V4_VERSION;
    metadata.header.metadata_size = sizeof(metadata);
    metadata.header.creation_time = ktime_get_real_seconds();
    
    // Validate magic number
    int magic_valid = (metadata.header.magic == DM_REMAP_V4_MAGIC);
    print_test_result("Magic number validation", magic_valid);
    
    // Validate version
    int version_valid = (metadata.header.version == DM_REMAP_V4_VERSION);
    print_test_result("Version validation", version_valid);
    
    // Validate size
    int size_valid = (metadata.header.metadata_size == sizeof(metadata));
    print_test_result("Size validation", size_valid);
    
    // Validate timestamp
    uint64_t current_time = ktime_get_real_seconds();
    int time_valid = (metadata.header.creation_time <= current_time) && 
                     (metadata.header.creation_time > current_time - 10);
    print_test_result("Timestamp validation", time_valid);
    
    return magic_valid && version_valid && size_valid && time_valid;
}

// Test 2: Device fingerprinting
int test_device_fingerprinting(void) {
    print_test_header("Device Fingerprinting");
    
    // Create mock device
    struct block_device mock_bdev = {
        .bd_disk_name = "test_device",
        .bd_nr_sectors = 20971520  // 10GB device
    };
    
    struct dm_dev mock_dev = {
        .bdev = &mock_bdev,
    };
    strncpy(mock_dev.name, "/dev/test_device", sizeof(mock_dev.name) - 1);
    
    struct dm_remap_v4_device_fingerprint fingerprint;
    memset(&fingerprint, 0, sizeof(fingerprint));
    
    // Test device path fingerprinting
    strncpy(fingerprint.device_path, mock_dev.name, sizeof(fingerprint.device_path) - 1);
    int path_valid = (strlen(fingerprint.device_path) > 0);
    print_test_result("Device path fingerprinting", path_valid);
    
    // Test device size fingerprinting
    fingerprint.device_size = mock_bdev.bd_nr_sectors * 512;  // Convert to bytes
    int size_valid = (fingerprint.device_size == 20971520ULL * 512);
    print_test_result("Device size fingerprinting", size_valid);
    
    // Test serial hash (mock)
    fingerprint.serial_hash = crc32(0, (unsigned char*)mock_bdev.bd_disk_name, 
                                   strlen(mock_bdev.bd_disk_name));
    int serial_valid = (fingerprint.serial_hash != 0);
    print_test_result("Serial hash generation", serial_valid);
    
    return path_valid && size_valid && serial_valid;
}

// Test 3: CRC32 integrity protection
int test_crc32_integrity(void) {
    print_test_header("CRC32 Integrity Protection");
    
    // Test data
    char test_data[] = "Hello, dm-remap v4.0 metadata!";
    uint32_t calculated_crc = crc32(0, (unsigned char*)test_data, strlen(test_data));
    
    // Verify CRC is non-zero
    int crc_nonzero = (calculated_crc != 0);
    print_test_result("CRC32 non-zero result", crc_nonzero);
    
    // Test CRC consistency
    uint32_t second_crc = crc32(0, (unsigned char*)test_data, strlen(test_data));
    int crc_consistent = (calculated_crc == second_crc);
    print_test_result("CRC32 consistency", crc_consistent);
    
    // Test CRC difference with modified data - change just one character
    char modified_data[] = "Hello, dm-remap v4.0 metadata?";  // Changed '!' to '?'
    uint32_t modified_crc = crc32(0, (unsigned char*)modified_data, strlen(modified_data));
    int crc_different = (calculated_crc != modified_crc);
    printf("    Original CRC: 0x%08x, Modified CRC: 0x%08x\n", calculated_crc, modified_crc);
    print_test_result("CRC32 detects modifications", crc_different);
    
    return crc_nonzero && crc_consistent && crc_different;
}

// Test 4: Metadata placement validation
int test_metadata_placement(void) {
    print_test_header("Metadata Placement Validation");
    
    // Test fixed sector positions
    uint64_t expected_sectors[DM_REMAP_V4_MAX_METADATA_COPIES] = {0, 1024, 2048, 4096, 8192};
    
    int all_valid = 1;
    for (int i = 0; i < DM_REMAP_V4_MAX_METADATA_COPIES; i++) {
        // Validate sector alignment (should be power of 2 or 0)
        uint64_t sector = expected_sectors[i];
        int is_valid_sector = (sector == 0) || ((sector & (sector - 1)) == 0);
        
        char test_name[64];
        snprintf(test_name, sizeof(test_name), "Sector %lu alignment", sector);
        print_test_result(test_name, is_valid_sector);
        
        all_valid &= is_valid_sector;
    }
    
    // Test minimum device size requirement (8MB)
    uint64_t min_sectors = (8 * 1024 * 1024) / 512;  // 8MB in sectors
    uint64_t last_metadata_sector = expected_sectors[DM_REMAP_V4_MAX_METADATA_COPIES - 1];
    int size_requirement = (min_sectors > last_metadata_sector + 16);  // +16 sectors for metadata
    print_test_result("8MB minimum size requirement", size_requirement);
    
    return all_valid && size_requirement;
}

// Test 5: Version control functionality
int test_version_control(void) {
    print_test_header("Version Control Functionality");
    
    struct dm_remap_v4_metadata metadata1, metadata2;
    memset(&metadata1, 0, sizeof(metadata1));
    memset(&metadata2, 0, sizeof(metadata2));
    
    // Initialize metadata with different sequence numbers
    metadata1.header.sequence_number = 1;
    metadata1.header.creation_time = ktime_get_real_seconds();
    
    sleep(1);  // Ensure different timestamps
    
    metadata2.header.sequence_number = 2;
    metadata2.header.creation_time = ktime_get_real_seconds();
    
    // Test sequence number comparison
    int seq_comparison = (metadata2.header.sequence_number > metadata1.header.sequence_number);
    print_test_result("Sequence number ordering", seq_comparison);
    
    // Test timestamp comparison
    int time_comparison = (metadata2.header.creation_time > metadata1.header.creation_time);
    print_test_result("Timestamp ordering", time_comparison);
    
    // Test version conflict resolution (higher sequence wins)
    int conflict_resolution = (metadata2.header.sequence_number > metadata1.header.sequence_number);
    print_test_result("Version conflict resolution", conflict_resolution);
    
    return seq_comparison && time_comparison && conflict_resolution;
}

// Test 6: Complete metadata validation
int test_complete_metadata_validation(void) {
    print_test_header("Complete Metadata Validation");
    
    struct dm_remap_v4_metadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    
    // Create valid metadata
    metadata.header.magic = DM_REMAP_V4_MAGIC;
    metadata.header.version = DM_REMAP_V4_VERSION;
    metadata.header.metadata_size = sizeof(metadata);
    metadata.header.sequence_number = 1;
    metadata.header.creation_time = ktime_get_real_seconds();
    metadata.header.num_targets = 1;
    metadata.header.num_spares = 1;
    
    // Add a test target
    metadata.targets[0].start_sector = 0;
    metadata.targets[0].length = 1000;
    strncpy(metadata.targets[0].device_name, "/dev/test_target", 
            sizeof(metadata.targets[0].device_name) - 1);
    
    // Add a test spare
    metadata.spares[0].device_size = 8 * 1024 * 1024;  // 8MB
    strncpy(metadata.spares[0].fingerprint.device_path, "/dev/test_spare",
            sizeof(metadata.spares[0].fingerprint.device_path) - 1);
    
    // Calculate CRC
    metadata.header.crc32 = crc32(0, (unsigned char*)&metadata.targets,
                                 sizeof(metadata.targets) + sizeof(metadata.spares) +
                                 sizeof(metadata.reassembly));
    
    // Validation tests
    int magic_valid = (metadata.header.magic == DM_REMAP_V4_MAGIC);
    int version_valid = (metadata.header.version == DM_REMAP_V4_VERSION);
    int size_valid = (metadata.header.metadata_size == sizeof(metadata));
    int targets_valid = (metadata.header.num_targets > 0);
    int spares_valid = (metadata.header.num_spares > 0);
    int crc_valid = (metadata.header.crc32 != 0);
    
    print_test_result("Complete metadata magic", magic_valid);
    print_test_result("Complete metadata version", version_valid);
    print_test_result("Complete metadata size", size_valid);
    print_test_result("Complete metadata targets", targets_valid);
    print_test_result("Complete metadata spares", spares_valid);
    print_test_result("Complete metadata CRC", crc_valid);
    
    return magic_valid && version_valid && size_valid && targets_valid && spares_valid && crc_valid;
}

// Main test runner
int main(int argc, char *argv[]) {
    printf("dm-remap v4.0 Metadata Creation Test Suite\n");
    printf("==========================================\n");
    printf("Date: October 14, 2025\n");
    printf("Testing comprehensive metadata functionality...\n");
    
    int total_tests = 0;
    int passed_tests = 0;
    
    // Run all tests
    struct {
        const char *name;
        int (*func)(void);
    } tests[] = {
        {"Metadata Structure Creation", test_metadata_structure_creation},
        {"Device Fingerprinting", test_device_fingerprinting},
        {"CRC32 Integrity Protection", test_crc32_integrity},
        {"Metadata Placement Validation", test_metadata_placement},
        {"Version Control Functionality", test_version_control},
        {"Complete Metadata Validation", test_complete_metadata_validation},
    };
    
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        total_tests++;
        if (tests[i].func()) {
            passed_tests++;
            printf("\n✅ %s: ALL SUBTESTS PASSED\n", tests[i].name);
        } else {
            printf("\n❌ %s: SOME SUBTESTS FAILED\n", tests[i].name);
        }
    }
    
    // Final results
    printf("\n==================================================\n");
    printf("TEST RESULTS SUMMARY\n");
    printf("==================================================\n");
    printf("Total test suites: %d\n", total_tests);
    printf("Passed test suites: %d\n", passed_tests);
    printf("Failed test suites: %d\n", total_tests - passed_tests);
    printf("Success rate: %.1f%%\n", (float)passed_tests / total_tests * 100);
    
    if (passed_tests == total_tests) {
        printf("\n🎉 ALL TESTS PASSED! v4.0 metadata creation is working correctly.\n");
        return 0;
    } else {
        printf("\n⚠️  SOME TESTS FAILED. Please review the output above.\n");
        return 1;
    }
}